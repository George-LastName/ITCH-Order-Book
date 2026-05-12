#include <cstdint> // uintX_t
#include <cstdlib>
#include <unistd.h> // close
#include <fcntl.h> // O_RDONLY
#include <iostream> // cout et. all
#include <string>
#include <sys/mman.h> // PROT_READ et. all & munmap
#include <sys/stat.h> // fstat
#include <unordered_map>
#include <memory> // unique_ptr


#include "message_types.h" // Messages
#include "order_book.h"
#include "database.h"

#ifdef PROF
#include <chrono>
#endif

#define HEADER_LENGTH 11
static constexpr size_t kTopN = 10;

enum class MarketState {
    kStartDay,
    kStartSystem,
    kStartMarket,
    kEndMarket,
    kEndSystem,
    kEndDay
};

int counts[256] = {0};
std::unordered_map<uint16_t, OrderBook> stock_books;

static std::string SanitiseTableName(const std::string& path) {
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



static inline void ParseMessage(uint8_t* ptr){

    const auto* header = reinterpret_cast<ItchHeader*>(ptr);
    ptr += HEADER_LENGTH;

    /* TODO: For SIMULATION, check if able to send message, if not wait until able.
     *  while(sim_time > header->timestamp){
     *      wait;
     *  }
     */

    counts[header->type]++;
    auto& stock = stock_books[header->GetLocate()];

    switch (header->type) {
        // [[X]] helps complier order the jump table for better efficiency.
        [[unlikely]] case 'S': {
            auto* mess = reinterpret_cast<const SysEvent*>(ptr);
            std::cout << header->GetTimeFromMid() << ": " << mess->event_code << "\n";
            break;
        }
        case 'R': {
            auto* mess = reinterpret_cast<const StockDir*>(ptr);
            stock = OrderBook(std::string_view(mess->stock, 8));
            break;
        }
        case 'H': {
            auto* mess = reinterpret_cast<StockTradingAction*>(ptr);
            stock.SetState(mess->trading_state);
            break;
        }
        case 'Y': {
            // auto* mess = reinterpret_cast<RegShoRestriction*>(ptr);
            break;
        }
        case 'L': {
            // auto* mess = reinterpret_cast<MarketParticipantPosition*>(ptr);
            break;
        }
        [[unlikely]] case 'V': {
            // auto* mess = reinterpret_cast<MwcbDeclineLevel*>(ptr);
            break;
        }
        [[unlikely]] case 'W': {
            // auto* mess = reinterpret_cast<MwcbStatus*>(ptr);
                break;
        }
        [[unlikely]] case 'K': {
            // auto* mess = reinterpret_cast<QuotingPeriodUpdate*>(ptr);
            break;
        }
        [[unlikely]] case 'J': {
            // auto* mess = reinterpret_cast<LuldAuctionCollar*>(ptr);
            break;
        }
        [[unlikely]] case 'h': {
            // auto* mess = reinterpret_cast<OperationalHalt*>(ptr);
            break;
        }
        [[likely]] case 'A': {
            auto* mess = reinterpret_cast<AddOrderNoMpid*>(ptr);
            stock.Add(mess);
            break;
        }
        case 'F': {
            auto* mess = reinterpret_cast<AddOrderMpid*>(ptr);
            stock.Add(mess);
            break;
        }
        case 'E': {
            auto* mess =  reinterpret_cast<OrderExecuted*>(ptr);
            stock.Execute(mess);
            break;
        }
        case 'C': {
            auto* mess = reinterpret_cast<OrderExecutedWithPrice*>(ptr);
            stock.Execute(mess);
            break;
        }
        case 'X': {
            auto* mess = reinterpret_cast<OrderCancel*>(ptr);
            stock.Cancel(mess);
            break;
        }
        [[likely]] case 'D': {
            auto* mess = reinterpret_cast<OrderDelete*>(ptr);
            stock.Delete(mess);
            break;
        }
        [[likely]] case 'U': {
            auto* mess = reinterpret_cast<OrderReplace*>(ptr);
            stock.Replace(mess);
            break;
        }
        case 'P': {
            // auto* mess = reinterpret_cast<TradeNonCross*>(ptr);
            break;
        }
        case 'Q': {
            // auto* mess = reinterpret_cast<CrossTrade*>(ptr);
            break;
        }
        [[unlikely]] case 'B': {
            // auto* mess = reinterpret_cast<BrokenTrade*>(ptr);
            break;
        }
        case 'I': {
            // auto* mess = reinterpret_cast<Noii*>(ptr);
            break;
        }
        [[unlikely]] case 'N': {
            // auto* mess = reinterpret_cast<Rpii*>(ptr);
            break;
        }
        [[unlikely]] case 'O': {
            // auto* mess = reinterpret_cast<DlcrPriceDiscovery*>(ptr);
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

    std::string database_name = "Market_Data_2";
    std::string table_name = SanitiseTableName(filepath);

    std::unique_ptr<Database> data_connection = Database::Create(DatabaseType::kClickhouse);

    data_connection->CreateDatabase(database_name);
    data_connection->CreateTables(table_name);

    std::uint8_t* filePtr = mapped_file;
    const std::uint8_t* file_end = mapped_file + file_size;

    static constexpr uint64_t kDeltaIntervalNs    =  1'000'000'000ULL; //  1 second
    static constexpr uint64_t kSnapshotIntervalNs = 60'000'000'000ULL; // 60 seconds

    uint64_t last_delta_ns    = 0;
    uint64_t last_snapshot_ns = 0;
    uint64_t curr_message_timestamp = 0;

    while (filePtr < file_end) {
        const uint16_t message_length = ntohs(*reinterpret_cast<const uint16_t*>(filePtr));
        filePtr += 2;

        curr_message_timestamp = reinterpret_cast<const ItchHeader*>(filePtr)->GetTimestamp();
        ParseMessage(filePtr);

        if (curr_message_timestamp - last_delta_ns >= kDeltaIntervalNs) {
            data_connection->WriteDelta(stock_books, curr_message_timestamp);
            last_delta_ns = curr_message_timestamp;
        }

        if (curr_message_timestamp - last_snapshot_ns >= kSnapshotIntervalNs) {
            data_connection->WriteSnapshot(stock_books, kTopN, curr_message_timestamp);
            last_snapshot_ns = curr_message_timestamp;
        }

        filePtr += message_length;
    }

    // Force final write
    data_connection->WriteDelta(stock_books, curr_message_timestamp, DbWriting::kOverrideLimit);
    data_connection->WriteSnapshot(stock_books, kTopN, curr_message_timestamp, DbWriting::kOverrideLimit);

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
