#include "clickhouse_database.h"
#include "order_book.h"


#include <mutex>
#include <thread>
#include <iostream>

#include <clickhouse/client.h>
#include <clickhouse/columns/array.h>
#include <clickhouse/columns/lowcardinality.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>

ClickhouseDatabase::ClickhouseDatabase()
        : client_{clickhouse::ClientOptions().SetHost("localhost")}{
    writer_thread_ = std::thread(&ClickhouseDatabase::ThreadWritingLoop, this);
    return;
};

ClickhouseDatabase::~ClickhouseDatabase(){
    Flush();
    done_.store(true);
    condition_variable_.notify_one();
    writer_thread_.join();
    std::cout << "Thread Done!\n";
}

void ClickhouseDatabase::CreateDatabase(std::string database_name){
    database_name_ = database_name;
    client_.Execute("CREATE DATABASE IF NOT EXISTS " + database_name_);
    return;
}

void ClickhouseDatabase::CreateTables(std::string table_name){
    table_name_ = SanitiseTableName(table_name);
    snapshot_table_ = database_name_ + "." + table_name_ + "_snapshots";
    delta_table_    = database_name_ + "." + table_name_ + "_deltas";
    client_.Execute(R"(
            CREATE TABLE IF NOT EXISTS )" + snapshot_table_ + R"(
            (
                stock_id        UInt16,
                stock_name      LowCardinality(String),
                timestamp_ns    UInt64,
                bid_prices      Array(UInt32),
                bid_shares      Array(UInt32),
                ask_prices      Array(UInt32),
                ask_shares      Array(UInt32)
            )
            ENGINE = ReplacingMergeTree()
            ORDER BY (stock_id, timestamp_ns)
            SETTINGS index_granularity = 8192
    )");

    client_.Execute(R"(
            CREATE TABLE IF NOT EXISTS )" + delta_table_ + R"(
            (
                timestamp_ns    UInt64,
                stock_id        UInt16,
                stock_name      LowCardinality(String),
                side            LowCardinality(String),
                price           UInt32,
                shares          UInt32
            )
            ENGINE = MergeTree()
            ORDER BY (stock_id, timestamp_ns, side, price)
            SETTINGS index_granularity = 8192
    )");

    return;
}

void ClickhouseDatabase::TakeSnapshot(const std::unordered_map<uint16_t, OrderBook>& books,
                                       const size_t book_depth,
                                       const uint64_t timestamp_ns){

    static constexpr int kFlushSnapshots = 10; // flush every 10 snapshots (~10 min of data)

    for (const auto& [stock_id, book] : books) {
        if (!book.IsInitialised()) continue;

        const auto snap = book.GetSnapshot(book_depth);
        pending_snapshots_.col_stock_id->Append(stock_id);
        pending_snapshots_.col_stock_name->Append(std::string_view(book.GetName()));
        pending_snapshots_.col_timestamp->Append(timestamp_ns);
        pending_snapshots_.col_bid_prices->Append(snap.bid_prices);
        pending_snapshots_.col_bid_shares->Append(snap.bid_shares);
        pending_snapshots_.col_ask_prices->Append(snap.ask_prices);
        pending_snapshots_.col_ask_shares->Append(snap.ask_shares);
    }

    if (++pending_snapshots_.buffered_snapshots > kFlushSnapshots) {
        FlushSnapshots();
    }
};

void ClickhouseDatabase::FlushSnapshots(){
    clickhouse::Block block;
    block.AppendColumn("stock_id",     pending_snapshots_.col_stock_id);
    block.AppendColumn("stock_name",   pending_snapshots_.col_stock_name);
    block.AppendColumn("timestamp_ns", pending_snapshots_.col_timestamp);
    block.AppendColumn("bid_prices",   pending_snapshots_.col_bid_prices);
    block.AppendColumn("bid_shares",   pending_snapshots_.col_bid_shares);
    block.AppendColumn("ask_prices",   pending_snapshots_.col_ask_prices);
    block.AppendColumn("ask_shares",   pending_snapshots_.col_ask_shares);

    EnqueueWrite({snapshot_table_, std::move(block) });

    pending_snapshots_ = PendingSnapshots{};
};

void ClickhouseDatabase::TakeDelta(std::unordered_map<uint16_t, OrderBook>& books,
                                    const uint64_t timestamp_ns){
    static constexpr int kFlushDeltas = 100; // flush every 100 delta rounds (~100s of data)

    for (auto& [stock_id, book] : books) {
        if (!book.IsInitialised() || !book.HasDeltas()) continue;

        const std::string_view name = book.GetName();

        for (const auto& [price, shares] : book.GetBidDeltas()) {
            pending_deltas_.col_timestamp->Append(timestamp_ns);
            pending_deltas_.col_stock_id->Append(stock_id);
            pending_deltas_.col_stock_name->Append(name);
            pending_deltas_.col_side->Append(std::string_view("B", 1));
            pending_deltas_.col_price->Append(price);
            pending_deltas_.col_shares->Append(shares);
        }
        for (const auto& [price, shares] : book.GetAskDeltas()) {
            pending_deltas_.col_timestamp->Append(timestamp_ns);
            pending_deltas_.col_stock_id->Append(stock_id);
            pending_deltas_.col_stock_name->Append(name);
            pending_deltas_.col_side->Append(std::string_view("S", 1));
            pending_deltas_.col_price->Append(price);
            pending_deltas_.col_shares->Append(shares);
        }

        book.ClearDeltas();
    }

    if(++pending_deltas_.buffered_deltas >= kFlushDeltas) {
        FlushDeltas();
    }
};

void ClickhouseDatabase::FlushDeltas(){
    clickhouse::Block block;
    block.AppendColumn("timestamp_ns", pending_deltas_.col_timestamp);
    block.AppendColumn("stock_id",     pending_deltas_.col_stock_id);
    block.AppendColumn("stock_name",   pending_deltas_.col_stock_name);
    block.AppendColumn("side",         pending_deltas_.col_side);
    block.AppendColumn("price",        pending_deltas_.col_price);
    block.AppendColumn("shares",       pending_deltas_.col_shares);

    EnqueueWrite({ delta_table_, std::move(block) });

    pending_deltas_ = PendingDeltas{};
};

void ClickhouseDatabase::EnqueueWrite(WaitingWrite write){
    {
    std::lock_guard lock(mutex_);
    writing_queue_.push(std::move(write));
    }
    condition_variable_.notify_one();
}

void ClickhouseDatabase::ThreadWritingLoop(){
    while(true){
        std::unique_lock lock(mutex_);
        condition_variable_.wait(lock, [&]{return !writing_queue_.empty() || done_.load(); });
        if (writing_queue_.empty() && done_.load()) break;

        std::vector<WaitingWrite> writes;
        while (!writing_queue_.empty()) {
            writes.push_back(std::move(writing_queue_.front()));
            writing_queue_.pop();
        }
        lock.unlock();

        for(auto& write : writes){
            client_.Insert(write.target_table, write.waiting_block);
        }
    }
};

void ClickhouseDatabase::Flush(){
    FlushDeltas();
    FlushSnapshots();
}
