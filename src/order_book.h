#ifndef ORDER_BOOK_H_
#define ORDER_BOOK_H_
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
#include "message_types.h"

struct BookSnapshot {
    std::vector<uint32_t> bid_prices;
    std::vector<uint32_t> bid_shares;
    std::vector<uint32_t> ask_prices;
    std::vector<uint32_t> ask_shares;
};

enum TradingState {
    kHalt,
    kPaused,
    kQuotation,
    kTrading
};

struct Order {
    char indicator; // Should be either B or S
    uint32_t shares;
    uint32_t price;
    std::string mpid;
};

class OrderBook {
private:
    std::string stock_name_;
    TradingState state_;
    std::map<std::uint32_t, std::uint32_t> bids_;    // price → total shares (buy side)
    std::map<std::uint32_t, std::uint32_t> asks_;    // price → total shares (sell side)
    std::unordered_map<std::uint64_t, Order> orders_; // order reference → order details

    // Per-price-level changes since the last delta flush.
    // Value = new total shares at that level; 0 means the level was removed.
    std::map<std::uint32_t, std::uint32_t> bid_deltas_;
    std::map<std::uint32_t, std::uint32_t> ask_deltas_;

    // Record the post-mutation state of a price level into the appropriate delta map.
    void RecordDelta(char side_indicator, std::uint32_t price);

public:
    OrderBook() {};
    OrderBook(std::string_view stock);
    // Stock Info
    const std::string& GetName() const { return stock_name_; }
    bool IsInitialised() const { return !stock_name_.empty(); }
    TradingState GetState() { return state_; }
    void SetState(char new_state);
    // Delta access
    bool HasDeltas() const { return !bid_deltas_.empty() || !ask_deltas_.empty(); }
    const std::map<std::uint32_t, std::uint32_t>& GetBidDeltas() const { return bid_deltas_; }
    const std::map<std::uint32_t, std::uint32_t>& GetAskDeltas() const { return ask_deltas_; }
    void ClearDeltas() { bid_deltas_.clear(); ask_deltas_.clear(); }
    // Snapshot
    BookSnapshot GetSnapshot(size_t top_n) const;

    // Order Book Updates
    //add Order - A, F
    template<typename T>
    void Add(const T* order);

    //execute Order - E, C
    void Execute(const OrderExecuted* order);
    void Execute(const OrderExecutedWithPrice* order);
    //cancel - X
    void Cancel(const OrderCancel* order);
    //delete - D
    void Delete(const OrderDelete* order);
    //replace - U
    void Replace(const OrderReplace* order);
};
#endif // ORDER_BOOK_H_
