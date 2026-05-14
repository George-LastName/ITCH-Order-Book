#ifndef CLICKHOUSE_DATABASE_H_
#define CLICKHOUSE_DATABASE_H_

#include <unordered_map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <queue>

#include <clickhouse/client.h>

#include "database.h"
#include "order_book.h"

class ClickhouseDatabase final : public Database {
private:
    clickhouse::Client client_;
    std::string database_name_;
    std::string table_name_;
    std::string snapshot_table_;
    std::string delta_table_;

    struct PendingSnapshots {
        std::shared_ptr<clickhouse::ColumnUInt16> col_stock_id;
        std::shared_ptr<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>> col_stock_name;
        std::shared_ptr<clickhouse::ColumnUInt64> col_timestamp;
        std::shared_ptr<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>> col_bid_prices;
        std::shared_ptr<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>> col_bid_shares;
        std::shared_ptr<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>> col_ask_prices;
        std::shared_ptr<clickhouse::ColumnArrayT<clickhouse::ColumnUInt32>> col_ask_shares;
        int buffered_snapshots = 0;
    };

    struct PendingDeltas {
        std::shared_ptr<clickhouse::ColumnUInt64> col_timestamp;
        std::shared_ptr<clickhouse::ColumnUInt16> col_stock_id;
        std::shared_ptr<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>> col_stock_name;
        std::shared_ptr<clickhouse::ColumnLowCardinalityT<clickhouse::ColumnString>> col_side;
        std::shared_ptr<clickhouse::ColumnUInt32> col_price;
        std::shared_ptr<clickhouse::ColumnUInt32> col_shares;
        int buffered_deltas = 0;
    };

    PendingSnapshots pending_snapshots_;
    PendingDeltas    pending_deltas_;

    struct WaitingWrite {
        std::string target_table;
        clickhouse::Block waiting_block;
    };
    std::queue<WaitingWrite> writing_queue_;
    std::mutex mutex_;
    std::condition_variable condition_variable_;
    std::atomic<bool> done_{false};
    std::thread writer_thread_;


    void FlushSnapshots();
    void FlushDeltas();
    void EnqueueWrite(WaitingWrite write);
public:
    ClickhouseDatabase();
    ~ClickhouseDatabase() override {};
    void CreateDatabase(std::string database_name) override;
    void CreateTables(std::string table_name) override;
    void TakeSnapshot(const std::unordered_map<uint16_t, OrderBook>& books,
                       const size_t book_depth,
                       const uint64_t timestamp_ns,
                       const DbWriting writing_mode = DbWriting::kFollowLimit) override;
    void TakeDelta(std::unordered_map<uint16_t, OrderBook>& books,
                    const uint64_t timestamp_ns,
                    const DbWriting writing_mode = DbWriting::kFollowLimit) override;
    void Flush();

    void ThreadWritingLoop();
};

#endif // CLICKHOUSE_DATABASE_H_
