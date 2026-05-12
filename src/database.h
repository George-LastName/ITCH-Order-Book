#ifndef DATABASE_H_
#define DATABASE_H_

#include <string>
#include <memory>
#include <unordered_map>
#include <cstdint>

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
    // Factory Design Pattern. Used to abstract child classes away.
    static std::unique_ptr<Database> Create(DatabaseType type);
};

#endif // DATABASE_H_
