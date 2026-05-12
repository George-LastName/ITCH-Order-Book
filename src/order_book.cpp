
#include <cstdlib>
#include <iostream>
#include <string>

#include "order_book.h"
#include "message_types.h"

OrderBook::OrderBook(std::string_view stock){
    state_ = TradingState::kHalt;
    stock_name_ = stock;
}

// Capture the post-mutation total at a price level into the delta map.
// If the level no longer exists in the book, records 0 (level removed).
void OrderBook::RecordDelta(char side_indicator, std::uint32_t price) {
    const auto& side = (side_indicator == 'B') ? bids_ : asks_;
    auto&     deltas = (side_indicator == 'B') ? bid_deltas_ : ask_deltas_;
    const auto it = side.find(price);
    deltas[price] = (it != side.end()) ? it->second : 0;
}

void OrderBook::SetState(char new_state){
    switch (new_state){
        case 'H': { state_ = TradingState::kHalt;      break; }
        case 'P': { state_ = TradingState::kPaused;    break; }
        case 'Q': { state_ = TradingState::kQuotation; break; }
        case 'T': { state_ = TradingState::kTrading;   break; }
        default:{
            std::cout << "Unknown Trading State for " << stock_name_ << " of " << new_state << "\n";
            exit(0);
        }
    }
    // State changes don't affect price levels — no delta recorded.
}

template<typename T>
void OrderBook::Add(const T* order){
    Order new_order;

    if constexpr (std::is_same_v<T, AddOrderMpid>) {
        new_order.mpid = std::string(order->attribution, 4);
    }
    new_order.indicator = order->buy_sell_indicator;
    new_order.shares    = ntohl(order->shares);
    new_order.price     = ntohl(order->price);

    orders_.insert({order->order_reference_number, new_order});

    if(new_order.indicator != 'B' && new_order.indicator != 'S'){
        std::cout << "Invalid Side for " << stock_name_ << " : |" << new_order.indicator << "|\n";
        exit(0);
    }

    auto& side = (new_order.indicator == 'B') ? bids_ : asks_;
    side[new_order.price] += new_order.shares;
    RecordDelta(new_order.indicator, new_order.price);
}

template void OrderBook::Add(const AddOrderMpid* order);
template void OrderBook::Add(const AddOrderNoMpid* order);

BookSnapshot OrderBook::GetSnapshot(size_t top_n) const {
    BookSnapshot snap;

    // Top N bids: highest prices first (reverse-iterate the ascending map)
    size_t count = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && count < top_n; ++it, ++count) {
        snap.bid_prices.push_back(it->first);
        snap.bid_shares.push_back(it->second);
    }

    // Top N asks: lowest prices first (forward-iterate)
    count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < top_n; ++it, ++count) {
        snap.ask_prices.push_back(it->first);
        snap.ask_shares.push_back(it->second);
    }

    return snap;
}

void OrderBook::Execute(const OrderExecuted* order){
    auto it = orders_.find(order->order_reference_number);
    if(it == orders_.end()){
        std::cout << "Order reference not found for execution: " << order->order_reference_number << "\n";
        return;
    }

    Order& exec_order = it->second;
    uint32_t executed_shares = ntohl(order->executed_shares);

    if(executed_shares > exec_order.shares){
        std::cout << "Executed shares exceed order shares for " << stock_name_ << "\n";
        return;
    }

    auto& side = (exec_order.indicator == 'B') ? bids_ : asks_;
    side[exec_order.price] -= executed_shares;
    if(side[exec_order.price] == 0){
        side.erase(exec_order.price);
    }
    RecordDelta(exec_order.indicator, exec_order.price);

    exec_order.shares -= executed_shares;
    if(exec_order.shares == 0){
        orders_.erase(it);
    }
}

void OrderBook::Execute(const OrderExecutedWithPrice* order){
    auto it = orders_.find(order->order_reference_number);
    if(it == orders_.end()){
        std::cout << "Order reference not found for execution with price: " << order->order_reference_number << "\n";
        return;
    }

    Order& exec_order = it->second;
    uint32_t executed_shares = ntohl(order->executed_shares);

    if(executed_shares > exec_order.shares){
        std::cout << "Executed shares exceed order shares for " << stock_name_ << "\n";
        return;
    }

    auto& side = (exec_order.indicator == 'B') ? bids_ : asks_;
    side[exec_order.price] -= executed_shares;
    if(side[exec_order.price] == 0){
        side.erase(exec_order.price);
    }
    RecordDelta(exec_order.indicator, exec_order.price);

    exec_order.shares -= executed_shares;
    if(exec_order.shares == 0){
        orders_.erase(it);
    }
}

void OrderBook::Cancel(const OrderCancel* order){
    auto it = orders_.find(order->order_reference_number);
    if(it == orders_.end()){
        std::cout << "Order reference not found for cancellation: " << order->order_reference_number << "\n";
        return;
    }

    Order& cancel_order = it->second;
    uint32_t canceled_shares = ntohl(order->canceled_shares);

    if(canceled_shares > cancel_order.shares){
        std::cout << "Canceled shares exceed order shares for " << stock_name_ << "\n";
        return;
    }

    auto& side = (cancel_order.indicator == 'B') ? bids_ : asks_;
    side[cancel_order.price] -= canceled_shares;
    if(side[cancel_order.price] == 0){
        side.erase(cancel_order.price);
    }
    RecordDelta(cancel_order.indicator, cancel_order.price);

    cancel_order.shares -= canceled_shares;
    if(cancel_order.shares == 0){
        orders_.erase(it);
    }
}

void OrderBook::Delete(const OrderDelete* order){
    auto it = orders_.find(order->order_reference_number);
    if(it == orders_.end()){
        std::cout << "Order reference not found for deletion: " << order->order_reference_number << "\n";
        return;
    }

    Order& delete_order = it->second;

    auto& side = (delete_order.indicator == 'B') ? bids_ : asks_;
    side[delete_order.price] -= delete_order.shares;
    if(side[delete_order.price] == 0){
        side.erase(delete_order.price);
    }
    RecordDelta(delete_order.indicator, delete_order.price);

    orders_.erase(it);
}

void OrderBook::Replace(const OrderReplace* order){
    auto it = orders_.find(order->original_order_reference_number);
    if(it == orders_.end()){
        std::cout << "Original order reference not found for replacement: " << order->original_order_reference_number << "\n";
        return;
    }

    Order& old_order = it->second;

    auto& side = (old_order.indicator == 'B') ? bids_ : asks_;
    side[old_order.price] -= old_order.shares;
    if(side[old_order.price] == 0){
        side.erase(old_order.price);
    }
    RecordDelta(old_order.indicator, old_order.price);

    Order new_order;
    new_order.indicator = old_order.indicator;
    new_order.shares    = ntohl(order->shares);
    new_order.price     = ntohl(order->price);
    new_order.mpid      = old_order.mpid;

    orders_.erase(it);
    orders_.insert({order->new_order_reference_number, new_order});

    side[new_order.price] += new_order.shares;
    RecordDelta(new_order.indicator, new_order.price);
}
