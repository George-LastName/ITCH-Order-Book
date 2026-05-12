#include "database.h"
#include "concrete_databases/clickhouse_database.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

std::unique_ptr<Database> Database::Create(DatabaseType type){
    switch (type){
        case DatabaseType::kClickhouse : {
            return std::make_unique<ClickhouseDatabase>();
        }
        default : {
            std::cout << "Unknown Database Type!\n";
            return nullptr;
        }
    }
}
