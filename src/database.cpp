#include "database.h"
#include <cstdlib>
#include <iostream>

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

std::unique_ptr<Database> Create(eDatabaseTypes type){
    switch (type){
        case eDatabaseTypes::kClickhouse : {
            return std::make_unique<ClickhouseDatabase>();
        }
        default : {
            std::cout << "Unknown Database Type!\n";
            EXIT_FAILURE;
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
}

void ClickhouseDatabase::CreateDatabase(std::string database_name){
    database_name_ = database_name;
    client_.Execute("CREATE DATABASE IF NOT EXISTS " + database_name_);
    return;
}

void ClickhouseDatabase::CreateTables(std::string table_name){
    table_name_ = SanitiseTableName(table_name);
    client_.Execute(R"(
            CREATE TABLE IF NOT EXISTS )" +
                    database_name_ + "." + table_name_ + R"(_snapshots
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
            CREATE TABLE IF NOT EXISTS )" +
                    database_name_ + "." + table_name_ + R"(_deltas
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

#pragma endregion Clickhouse_Database
