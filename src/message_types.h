#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H
#include <cstdint> // uintX_t
#include <chrono> // chrono:: for nanoseconds from midnight
#include <cstring> // memcpy
#include <string_view>
#include <arpa/inet.h> // ntohl (big endian to little)

// template to help easy std::cout << of the message if wanted.
template<typename ItchType>
struct ItchMessage {
    friend std::ostream& operator<<(std::ostream& os, const ItchType* message){
        return message->Print(os);
    }
};

struct [[gnu::packed]] ItchHeader {
    char type;
    std::uint16_t locate;
    std::uint16_t track_num;
    std::uint8_t timestamp[6];

    std::uint64_t GetTimestamp() const{
        std::uint64_t ti_st = 0;
        std::memcpy(&ti_st, timestamp, 8);
        ti_st = __builtin_bswap64(ti_st);
        return ti_st >> 16;
    }

    auto GetTimeFromMid() const{
        std::chrono::nanoseconds duration(GetTimestamp());
        std::chrono::hh_mm_ss real_time{duration};
        return real_time;
    }

    auto GetLocate() const{
        return ntohs(locate);
    }

    auto GetTrackNum() const{
        return ntohs(track_num);
    }
};

struct [[gnu::packed]] SysEvent {
    char event_code;
    /* O = Mess. Start,
     * S = Sys Hours Start,
     * Q = Market Open,
     * M = Market Close,
     * E = Sys Hours End,
     * C = Mess. End
     */
};

struct [[gnu::packed]] StockDir : ItchMessage<StockDir> {
    char stock[8];
    char market_cat;
    char fin_stat_ind;
    std::uint32_t rnd_lot_size;
    char rnd_lots_only;
    char issue_class;
    char issue_sub[2];
    char auth;
    char srt_thr_ind;
    char ipo_flg;
    char luld_ref;
    char etp_flg;
    std::uint32_t etp_lev;
    char inv_ind;

    std::ostream& Print(std::ostream& os) const {
        return os
        << "  Stock: " << std::string_view(stock, 8) << "\n"
        << "  Market Category: " << market_cat << "\n"
        << "  Financial Status Indicator: " << fin_stat_ind << "\n"
        << "  Round Lot Size: " << ntohl(rnd_lot_size) << "\n"
        << "  Round Lots Only: " << rnd_lots_only << "\n"
        << "  Issue Classification: " << issue_class << "\n"
        << "  Issue Sub-Type: " << std::string_view(issue_sub, 2) << "\n"
        << "  Authenticity: " << auth << "\n"
        << "  Short Sale Threshold Indicator: " << srt_thr_ind << "\n"
        << "  IPO Flag: " << ipo_flg << "\n"
        << "  LULD Reference Price Tier: " << luld_ref << "\n"
        << "  ETP Flag: " << etp_flg << "\n"
        << "  ETP Leverage Factor: " << ntohl(etp_lev) << "\n"
        << "  Inverse Indicator: " << inv_ind << "\n";
    }

    std::string_view GetStock() { return std::string_view(stock, 8); }
};

struct StockDirHash {
    std::size_t operator()(const StockDir* sd) const{
        return std::hash<std::string_view>{}(std::string_view(sd->stock));
    }
};

struct StockDirEqual {
    bool operator()(const StockDir* s1, const StockDir* s2) const{
        return strncmp(s1->stock, s2->stock, 8) == 0;
    }
};

struct [[gnu::packed]] StockTradingAction {
    char stock[8];
    char trading_state; // 'H'=Halted, 'P'=Paused, 'Q'=Quotation, 'T'=Trading
    char reserved;
    char reason[4];
};

struct [[gnu::packed]] RegShoRestriction {
    char stock[8];
    char reg_sho_action; // '0'=No price test, '1'=In effect, '2'=Remains in effect
};

struct [[gnu::packed]] MarketParticipantPosition {
    char mpid[4];
    char stock[8];
    char primary_market_maker;
    char market_maker_mode;
    char market_participant_state;
};

struct [[gnu::packed]] MwcbDeclineLevel {
    std::uint64_t level_1;
    std::uint64_t level_2;
    std::uint64_t level_3;
};

struct [[gnu::packed]] MwcbStatus {
    char breached_level;
};

struct [[gnu::packed]] QuotingPeriodUpdate {
    char stock[8];
    std::uint32_t ipo_quot_release_time;
    char ipo_quot_release_qualifier;
    std::uint32_t ipo_price;
};

struct [[gnu::packed]] LuldAuctionCollar {
    char stock[8];
    std::uint32_t auction_collar_ref_price;
    std::uint32_t upper_auction_collar_price;
    std::uint32_t lower_auction_collar_price;
    std::uint32_t auction_collar_ext;
};

struct [[gnu::packed]] OperationalHalt {
    char stock[8];
    char market_code;
    char halt_action;
};

struct [[gnu::packed]] AddOrderNoMpid {
    std::uint64_t order_reference_number;
    char buy_sell_indicator; // 'B'=Buy, 'S'=Sell
    std::uint32_t shares;
    char stock[8];
    std::uint32_t price; // Price integer (divide by 10,000 for decimal)
};

struct [[gnu::packed]] AddOrderMpid {
    std::uint64_t order_reference_number;
    char buy_sell_indicator;
    std::uint32_t shares;
    char stock[8];
    std::uint32_t price; // Price integer (divide by 10,000 for decimal)
    char attribution[4];
};

struct [[gnu::packed]] OrderExecuted {
    std::uint64_t order_reference_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
};

struct [[gnu::packed]] OrderExecutedWithPrice {
    std::uint64_t order_reference_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
    char printable; // 'Y' or 'N'
    std::uint32_t execution_price;
};

struct [[gnu::packed]] OrderCancel {
    std::uint64_t order_reference_number;
    std::uint32_t canceled_shares;
};

struct [[gnu::packed]] OrderDelete {
    std::uint64_t order_reference_number;
};

struct [[gnu::packed]] OrderReplace {
    std::uint64_t original_order_reference_number;
    std::uint64_t new_order_reference_number;
    std::uint32_t shares;
    std::uint32_t price;
};

struct [[gnu::packed]] TradeNonCross {
    std::uint64_t order_reference_number; // Always 0 for this message
    char buy_sell_indicator;
    std::uint32_t shares;
    char stock[8];
    std::uint32_t price;
    std::uint64_t match_number;
};

struct [[gnu::packed]] CrossTrade {
    std::uint64_t shares;
    char stock[8];
    std::uint32_t cross_price;
    std::uint64_t match_number;
    char cross_type; // 'O'=Open, 'C'=Close, 'H'=Halt/IPO, 'I'=Intraday
};

struct [[gnu::packed]] BrokenTrade {
    std::uint64_t match_number;
};

struct [[gnu::packed]] Noii {
    std::uint64_t paired_shares;
    std::uint64_t imbalance_shares;
    char imbalance_direction; // 'B', 'S', 'N', 'O'
    char stock[8];
    std::uint32_t far_price;
    std::uint32_t near_price;
    std::uint32_t current_reference_price;
    char cross_type;
    char price_variation_indicator;
};

struct [[gnu::packed]] Rpii { // Retail Price Improvement Indicator
    char stock[8];
    char interest_flag; // 'B', 'S', 'A', 'N'
};

struct [[gnu::packed]] DlcrPriceDiscovery {
    char stock[8];
    char open_eligibility_status;
    std::uint32_t min_allow_price;
    std::uint32_t max_allow_price;
    std::uint32_t near_exe_price;
    std::uint64_t near_exe_time;
    std::uint32_t lower_price_range_collar;
    std::uint32_t upper_price_range_collar;
};
#endif // MESSAGE_TYPES_H
