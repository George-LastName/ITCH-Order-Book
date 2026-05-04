#include <cstdint> // uintX_t
#include <cstdlib>
#include <unistd.h> // close
#include <fcntl.h> // O_RDONLY

#include <iostream> // cout et. all
#include <string>

#include <sys/mman.h> // PROT_READ et. all & munmap
#include <sys/stat.h> // fstat

#include <unordered_map>

#include "MessageTypes.h" // Messages
#include "OrderBook.h"
#include "clickhouse/ClickhouseOrderBook.h"

// Clickhouse
#include <clickhouse/client.h>

#ifdef PROF
#include <chrono>
#endif

#define HEADER_LENGTH 11
static constexpr size_t TOP_N = 10;

enum class Market_State {
    START_DAY,
    START_SYSTEM,
    START_MARKET,
    END_MARKET,
    END_SYSTEM,
    END_DAY
};

int counts[256] = {0};
std::unordered_map<uint16_t, Order_Book> stock_books;

static std::string sanitise_table_name(const std::string& path) {
    // Strip directory prefix — take only the filename
    const size_t slash = path.rfind('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    // Replace any character that isn't alphanumeric or underscore with '_'
    for (char& c : name) {
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



static inline void parse_message(uint8_t* ptr){

    const auto* Header = reinterpret_cast<ITCH_Header*>(ptr);
    ptr += HEADER_LENGTH;

    /* TODO: For SIMULATION, check if able to send message, if not wait until able.
     *  while(sim_time > header->timestamp){
     *      wait;
     *  }
     */

    counts[Header->type]++;
    auto& stock = stock_books[Header->get_locate()];

    switch (Header->type) {
        // [[X]] helps complier order the jump table for better efficiency.
        [[unlikely]] case 'S': {
            auto* Mess = reinterpret_cast<const Sys_Event*>(ptr);
            std::cout << Header->get_time_from_mid() << ": " << Mess->event_code << "\n";
            break;
        }
        case 'R': {
            auto* Mess = reinterpret_cast<const Stock_Dir*>(ptr);
            stock = Order_Book(std::string_view(Mess->stock, 8));
            break;
        }
        case 'H': {
            auto* Mess = reinterpret_cast<Stock_Trading_Action*>(ptr);
            stock.Set_State(Mess->trading_state);
            break;
        }
        case 'Y': {
            // auto* Mess = reinterpret_cast<Reg_Sho_Restriction*>(ptr);
            break;
        }
        case 'L': {
            // auto* Mess = reinterpret_cast<Market_Participant_Position*>(ptr);
            break;
        }
        [[unlikely]] case 'V': {
            // auto* Mess = reinterpret_cast<MWCB_Decline_Level*>(ptr);
            break;
        }
        [[unlikely]] case 'W': {
            // auto* Mess = reinterpret_cast<MWCB_Status*>(ptr);
                break;
        }
        [[unlikely]] case 'K': {
            // auto* Mess = reinterpret_cast<Quoting_Period_Update*>(ptr);
            break;
        }
        [[unlikely]] case 'J': {
            // auto* Mess = reinterpret_cast<LULD_Auction_Collar*>(ptr);
            break;
        }
        [[unlikely]] case 'h': {
            // auto* Mess = reinterpret_cast<Operational_Halt*>(ptr);
            break;
        }
        [[likely]] case 'A': {
            auto* Mess = reinterpret_cast<Add_Order_No_MPID*>(ptr);
            stock.Add(Mess);
            break;
        }
        case 'F': {
            auto* Mess = reinterpret_cast<Add_Order_MPID*>(ptr);
            stock.Add(Mess);
            break;
        }
        case 'E': {
            auto* Mess =  reinterpret_cast<Order_Executed*>(ptr);
            stock.Execute(Mess);
            break;
        }
        case 'C': {
            auto* Mess = reinterpret_cast<Order_Executed_With_Price*>(ptr);
            stock.Execute(Mess);
            break;
        }
        case 'X': {
            auto* Mess = reinterpret_cast<Order_Cancel*>(ptr);
            stock.Cancel(Mess);
            break;
        }
        [[likely]] case 'D': {
            auto* Mess = reinterpret_cast<Order_Delete*>(ptr);
            stock.Delete(Mess);
            break;
        }
        [[likely]] case 'U': {
            auto* Mess = reinterpret_cast<Order_Replace*>(ptr);
            stock.Replace(Mess);
            break;
        }
        case 'P': {
            // auto* Mess = reinterpret_cast<Trade_Non_Cross*>(ptr);
            break;
        }
        case 'Q': {
            // auto* Mess = reinterpret_cast<Cross_Trade*>(ptr);
            break;
        }
        [[unlikely]] case 'B': {
            // auto* Mess = reinterpret_cast<Broken_Trade*>(ptr);
            break;
        }
        case 'I': {
            // auto* Mess = reinterpret_cast<NOII*>(ptr);
            break;
        }
        [[unlikely]] case 'N': {
            // auto* Mess = reinterpret_cast<RPII*>(ptr);
            break;
        }
        [[unlikely]] case 'O': {
            // auto* Mess = reinterpret_cast<DLCR_Price_Discovery*>(ptr);
            break;
        }
        default:{
            std::cout << "ERROR: Unknown Message Type! : " << ptr[0] << std::endl;
            break;
        }
    }
}

int main(int argc, char* argv[]){
#ifdef PROF
    std::cout << "PROFILING!\n";
    auto start = std::chrono::high_resolution_clock::now();
#endif
    // Check file path has been provided
    if (argc != 2) {
        std::cout << "Second argument must be file path/filename." << std::endl;
        exit(EXIT_FAILURE);
    }

    const char* filepath = argv[1];

    // Open file and get file size
    int rdonly_file = open(filepath, O_RDONLY);
    if (rdonly_file == -1){
        std::cout << "Failed to open file" << std::endl;
        exit(EXIT_FAILURE);
    }

    struct stat file_stats;
    if(fstat(rdonly_file, &file_stats) == -1){
        std::cout << "Failed to get file size." << std::endl;
        close(rdonly_file);
        exit(EXIT_FAILURE);
    }

    const off_t file_size = file_stats.st_size;

    // Get pointer to start of provided file
    std::uint8_t* mapped_file = reinterpret_cast<std::uint8_t*>(mmap((caddr_t)0, file_size, PROT_READ, MAP_SHARED, rdonly_file, 0));
    if(mapped_file == MAP_FAILED){
        std::cout << "Failed to mmap file.\n" << std::endl;
        close(rdonly_file);
        exit(EXIT_FAILURE);
    }
    close(rdonly_file);

    std::string database_name = "Market_Data";
    std::string table_name = sanitise_table_name(filepath);

    {
        clickhouse::Client client{clickhouse::ClientOptions().SetHost("localhost")};
        client.Execute(
            "CREATE DATABASE IF NOT EXISTS " + database_name
        );

        // Full book snapshots — taken every 60 s as a recovery baseline.
        // ReplacingMergeTree deduplicates on (stock_id, timestamp_ns) so re-runs
        // of the same file are idempotent.
        client.Execute(R"(
            CREATE TABLE IF NOT EXISTS )" + database_name + "." + table_name + R"(_snapshots
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

        // Per-price-level deltas — one row per changed level per second.
        // shares = 0 means the level was removed.
        // MergeTree (no dedup) because each delta is a distinct event.
        client.Execute(R"(
            CREATE TABLE IF NOT EXISTS )" + database_name + "." + table_name + R"(_deltas
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

        std::uint8_t* filePtr = mapped_file;
        const std::uint8_t* file_end = mapped_file + file_size;
        const std::string snapshot_table = database_name + "." + table_name + "_snapshots";
        const std::string delta_table    = database_name + "." + table_name + "_deltas";

        static constexpr uint64_t DELTA_INTERVAL_NS    =  1'000'000'000ULL; //  1 second
        static constexpr uint64_t SNAPSHOT_INTERVAL_NS = 60'000'000'000ULL; // 60 seconds

        uint64_t last_delta_ns    = 0;
        uint64_t last_snapshot_ns = 0;
        uint64_t ts = 0;

        // Loop through file
        while (filePtr < file_end) {
            const uint16_t message_l = ntohs(*reinterpret_cast<const uint16_t*>(filePtr));
            filePtr += 2;

            ts = reinterpret_cast<const ITCH_Header*>(filePtr)->get_timestamp();

            parse_message(filePtr);

            // Flush changed price levels every second
            if (ts - last_delta_ns >= DELTA_INTERVAL_NS) {
                flush_deltas(client, delta_table, ts, stock_books);
                last_delta_ns = ts;
            }

            // Write full book snapshots every 60 seconds as a recovery baseline
            if (ts - last_snapshot_ns >= SNAPSHOT_INTERVAL_NS) {
                snapshot_all_books(client, snapshot_table, ts, stock_books, TOP_N);
                last_snapshot_ns = ts;
            }

            filePtr += message_l;
        }

        // Final flush of any remaining buffered data
        flush_deltas(client, delta_table, ts, stock_books, true);
        snapshot_all_books(client, snapshot_table, ts, stock_books, TOP_N, true);

    } // client destroyed here — sends proper TCP disconnect before munmap/return

    std::cout << "\n";
    long sum = 0;
    for (int i = 0; i < 256; i++){
        if (counts[i] != 0) std::cout << static_cast<char>(i) << ": " << counts[i] << "\n";
        sum += counts[i];
    }

    std::cout << "\n";

    if(munmap(mapped_file, file_size) == -1){
        std::cout << "Failed to munmap." << std::endl;
    }
#ifdef PROF
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop-start);
    std::cout << "TIME: " << duration.count() << "\n";
#endif
    return 0;
}
