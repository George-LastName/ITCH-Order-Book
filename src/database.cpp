#include "database.h"
#include "OrderBook.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <string>
#include <chrono>


#include <clickhouse/client.h>
#include <clickhouse/columns/array.h>
#include <clickhouse/columns/lowcardinality.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>

#pragma region Database_Parent

std::string Database::SanitiseTableName(const std::string &path) {
    // Strip directory prefix — take only the filename
    const size_t slash = path.rfind('/');
    std::string name =
        (slash == std::string::npos) ? path : path.substr(slash + 1);

    // Replace any character that isn't alphanumeric or underscore with '_'
    for (char &c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }

    // Prefix with '_' if the name starts with a digit
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        name = "_" + name;
    }

    return name;
}

std::unique_ptr<Database> Database::Create(eDatabaseTypes type){
    switch (type){
        case eDatabaseTypes::kClickhouse : {
            return std::make_unique<ClickhouseDatabase>();
        }
        default : {
            std::cout << "Unknown Database Type!\n";
            return nullptr;
        }
    }
}

#pragma endregion Database_Parent
#pragma region Clickhouse_Database

ClickhouseDatabase::ClickhouseDatabase()
        : client_{clickhouse::ClientOptions().SetHost("localhost")}{
    return;
};

void ClickhouseDatabase::Disconnect(){
    client_.Ping();
    std::cout << "Clickhouse PING\n";
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

void ClickhouseDatabase::WriteSnapshot(std::unordered_map<uint16_t, Order_Book>& books,
                                       size_t book_depth,
                                       uint64_t timestamp_ns,
                                       eDBWriting writing_mode){

    static auto col_stock_id   = std::make_shared<clickhouse::ColumnUInt16>();
    static auto col_stock_name = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    static auto col_timestamp  = std::make_shared<clickhouse::ColumnUInt64>();
    static auto col_bid_prices = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    static auto col_bid_shares = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    static auto col_ask_prices = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    static auto col_ask_shares = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    static int buffered_snapshots = 0;

    static constexpr int FLUSH_EVERY = 10; // flush every 10 snapshots (~10 min of data)

    for (const auto& [stock_id, book] : books) {
        if (!book.IsInitialised()) continue;

        const auto snap = book.GetSnapshot(book_depth);
        col_stock_id->Append(stock_id);
        col_stock_name->Append(std::string_view(book.GetName()));
        col_timestamp->Append(timestamp_ns);
        col_bid_prices->Append(snap.bid_prices);
        col_bid_shares->Append(snap.bid_shares);
        col_ask_prices->Append(snap.ask_prices);
        col_ask_shares->Append(snap.ask_shares);
    }

    buffered_snapshots++;
    if (writing_mode == eDBWriting::kFollowLimit && buffered_snapshots < FLUSH_EVERY) return;
    if (col_stock_id->Size() == 0) return;

    clickhouse::Block block;
    block.AppendColumn("stock_id",     col_stock_id);
    block.AppendColumn("stock_name",   col_stock_name);
    block.AppendColumn("timestamp_ns", col_timestamp);
    block.AppendColumn("bid_prices",   col_bid_prices);
    block.AppendColumn("bid_shares",   col_bid_shares);
    block.AppendColumn("ask_prices",   col_ask_prices);
    block.AppendColumn("ask_shares",   col_ask_shares);

    client_.Insert(snapshot_table_, block);
    std::chrono::nanoseconds duration(timestamp_ns);
    std::chrono::hh_mm_ss real_time{duration};
    std::cout << real_time <<  ": Flushed " << buffered_snapshots << " snapshots, rows: " << col_stock_id->Size() << "\n";

    col_stock_id   = std::make_shared<clickhouse::ColumnUInt16>();
    col_stock_name = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    col_timestamp  = std::make_shared<clickhouse::ColumnUInt64>();
    col_bid_prices = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    col_bid_shares = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    col_ask_prices = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    col_ask_shares = std::make_shared<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>>();
    buffered_snapshots = 0;
};

void ClickhouseDatabase::WriteDelta(std::unordered_map<uint16_t, Order_Book>& books,
                                    uint64_t timestamp_ns,
                                    eDBWriting writing_mode){
    static auto col_timestamp  = std::make_shared<clickhouse::ColumnUInt64>();
    static auto col_stock_id   = std::make_shared<clickhouse::ColumnUInt16>();
    static auto col_stock_name = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    static auto col_side       = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    static auto col_price      = std::make_shared<clickhouse::ColumnUInt32>();
    static auto col_shares     = std::make_shared<clickhouse::ColumnUInt32>();
    static int buffered_rounds = 0;

    static constexpr int FLUSH_EVERY = 100; // flush every 100 delta rounds (~100s of data)

    for (auto& [stock_id, book] : books) {
        if (!book.IsInitialised() || !book.HasDeltas()) continue;

        const std::string_view name = book.GetName();

        for (const auto& [price, shares] : book.GetBidDeltas()) {
            col_timestamp->Append(timestamp_ns);
            col_stock_id->Append(stock_id);
            col_stock_name->Append(name);
            col_side->Append(std::string_view("B", 1));
            col_price->Append(price);
            col_shares->Append(shares);
        }
        for (const auto& [price, shares] : book.GetAskDeltas()) {
            col_timestamp->Append(timestamp_ns);
            col_stock_id->Append(stock_id);
            col_stock_name->Append(name);
            col_side->Append(std::string_view("S", 1));
            col_price->Append(price);
            col_shares->Append(shares);
        }

        book.ClearDeltas();
    }

    buffered_rounds++;
    if (writing_mode == eDBWriting::kFollowLimit && buffered_rounds < FLUSH_EVERY) return;
    if (col_timestamp->Size() == 0) return;

    clickhouse::Block block;
    block.AppendColumn("timestamp_ns", col_timestamp);
    block.AppendColumn("stock_id",     col_stock_id);
    block.AppendColumn("stock_name",   col_stock_name);
    block.AppendColumn("side",         col_side);
    block.AppendColumn("price",        col_price);
    block.AppendColumn("shares",       col_shares);

    client_.Insert(delta_table_, block);
    //std::cout << "Flushed " << buffered_rounds << " delta rounds, rows: " << col_timestamp->Size() << "\n";

    col_timestamp  = std::make_shared<clickhouse::ColumnUInt64>();
    col_stock_id   = std::make_shared<clickhouse::ColumnUInt16>();
    col_stock_name = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    col_side       = std::make_shared<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>>();
    col_price      = std::make_shared<clickhouse::ColumnUInt32>();
    col_shares     = std::make_shared<clickhouse::ColumnUInt32>();
    buffered_rounds = 0;
};

#pragma endregion Clickhouse_Database
