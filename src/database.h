#ifndef DATABASE_H_
#define DATABASE_H_

#include <string>
#include <memory>
#include <unordered_map>

#include <clickhouse/client.h> // Clickhouse

#include "order_book.h"

enum class DatabaseType {
    kClickhouse,
};

enum class DbWriting { kOverrideLimit, kFollowLimit };

class Database {
public:
    Database(){}
    virtual ~Database() = default;
    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual void CreateDatabase(std::string) = 0;
    virtual void CreateTables(std::string) = 0;
    virtual void WriteSnapshot(std::unordered_map<uint16_t, OrderBook>& books,
                               size_t book_depth,
                               uint64_t timestamp_ns,
                               DbWriting writing_mode = DbWriting::kFollowLimit) = 0;
    virtual void WriteDelta(std::unordered_map<uint16_t, OrderBook>& books,
                            uint64_t timestamp_ns,
                            DbWriting writing_mode = DbWriting::kFollowLimit) = 0;
    static std::string SanitiseTableName(const std::string& path);
    // Factor Design Pattern. Used to abstract child classes away.
    static std::unique_ptr<Database> Create(DatabaseType type);
};

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

// class KdbDatabase : Database{
//
// }
#endif // DATABASE_H_
