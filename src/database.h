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

struct DeltaBatch {
    virtual ~DeltaBatch() = default;
};

struct SnapshotBatch {
    virtual ~SnapshotBatch() = default;
};


class Database {
public:
    Database(){}
    virtual ~Database() = default;
    virtual void CreateDatabase(std::string) = 0;
    virtual void CreateTables(std::string) = 0;
    virtual void TakeSnapshot(const std::unordered_map<uint16_t, OrderBook>& books,
                               const size_t book_depth,
                               const uint64_t timestamp_ns) = 0;
    virtual void TakeDelta(std::unordered_map<uint16_t, OrderBook>& books,
                            const uint64_t timestamp_ns) = 0;
    virtual void Flush() = 0;

    static std::string SanitiseTableName(const std::string& path);
    // Factory Design Pattern. Used to abstract child classes away.
    static std::unique_ptr<Database> Create(DatabaseType type);
};

#endif // DATABASE_H_
