#ifndef DATABASE_H_
#define DATABASE_H_

#include <string>
#include <memory>

#include <clickhouse/client.h> // Clickhouse

enum class eDatabaseTypes{
    kClickhouse,
};

class Database {
public:
    Database(){}
    virtual ~Database() = default;
    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual void CreateDatabase(std::string) = 0;
    virtual void CreateTables(std::string) = 0;
    // virtual void LogSnapshot() = 0;
    // virtual void LogDelta() = 0;
    static std::string SanitiseTableName(const std::string& path);
    // Factor Design Pattern. Used to abstract child classes away.
    static std::unique_ptr<Database> Create(eDatabaseTypes type);
};


class ClickhouseDatabase : Database {
private:
    clickhouse::Client client_;
    std::string database_name_;
    std::string table_name_;
public:
    ClickhouseDatabase();
    void Connect() override;
    void Disconnect() override;
    void CreateDatabase(std::string database_name) override;
    void CreateTables(std::string table_name) override;
};


// class kdbDatabase : Database{
//
// }
#endif // DATABASE_H_
