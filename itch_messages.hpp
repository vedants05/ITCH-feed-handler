#ifndef ITCH_MESSAGES_HPP
#define ITCH_MESSAGES_HPP

#include <cstdint>

enum class MsgType : char {
    SystemEvent    = 'S',
    StockDirectory = 'R',
    AddOrder       = 'A',
    DeleteOrder    = 'D',
    ExecuteOrder   = 'E',
    CancelOrder = 'X',
    ReplaceOrder = 'U'
};

// __attribute__((packed)) ensures struct is stored contiguosly in memory.
struct __attribute__((packed)) SystemEventMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    char event_code;
};

struct __attribute__((packed)) StockDirectoryMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    char stock[8];
    char market_category;
    char financial_status_indicator;
    uint32_t round_lot_size;
    char round_lots_only;
    char issue_classification;
    char issue_subtype[2];
    char authenticity;
    char short_sale_threshold_indicator;
    char ipo_flag;
    char LULD_ref_price_tier;
    char ETP_flag;
    uint32_t ETP_leverage_factor;
    char inverse_indicator;
};

struct __attribute__((packed)) AddOrderMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    uint64_t order_ref_num;
    char buy_sell_indicator;
    uint32_t shares;
    char stock[8];
    uint32_t price;
};

struct __attribute__((packed)) DeleteOrderMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    uint64_t order_ref_num;
};

struct __attribute__((packed)) ExecuteOrderMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    uint64_t order_ref_num;
    uint32_t executed_shares;
    uint64_t match_num;
};

struct __attribute__((packed)) CancelOrderMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    uint64_t order_ref_num;
    uint32_t cancelled_shares;
};

struct __attribute__((packed)) ReplaceOrderMessage {
    char msg_type;
    uint16_t stock_locate;
    uint16_t tracking_num;
    uint8_t timestamp[6];
    uint64_t original_order_ref_num;
    uint64_t new_order_ref_num;
    uint32_t shares;
    uint32_t price;
};

// Convert timestamp to uint64_t
inline uint64_t read_timestamp(const uint8_t ts[6]) {
    return (uint64_t)ts[0] << 40 |
           (uint64_t)ts[1] << 32 |
           (uint64_t)ts[2] << 24 |
           (uint64_t)ts[3] << 16 |
           (uint64_t)ts[4] <<  8 |
           (uint64_t)ts[5];
}

#endif