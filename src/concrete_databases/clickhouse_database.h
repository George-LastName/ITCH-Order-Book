#ifndef CLICKHOUSE_DATABASE_H_
#define CLICKHOUSE_DATABASE_H_

#include <unordered_map>
#include <string>

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
public:
    ClickhouseDatabase();
    ~ClickhouseDatabase() override {};
    void Connect() override {};
    void Disconnect() override;
    void CreateDatabase(std::string database_name) override;
    void CreateTables(std::string table_name) override;
    void WriteSnapshot(std::unordered_map<uint16_t, OrderBook>& books,
                       size_t book_depth,
                       uint64_t timestamp_ns,
                       DbWriting writing_mode = DbWriting::kFollowLimit) override;
    void WriteDelta(std::unordered_map<uint16_t, OrderBook>& books,
                    uint64_t timestamp_ns,
                    DbWriting writing_mode = DbWriting::kFollowLimit) override;
};

#endif // CLICKHOUSE_DATABASE_H_
