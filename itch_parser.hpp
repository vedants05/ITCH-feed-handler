#ifndef ITCH_PARSER_HPP
#define ITCH_PARSER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "order_book.hpp"

static inline uint64_t betoh64(uint64_t x) {
    return ((uint64_t)ntohl(x & 0xFFFFFFFF) << 32) | ntohl(x >> 32);
}

class ItchParser {
    public:
        void process(const uint8_t* data, size_t len);

    private:
        std::unordered_map<uint16_t, OrderBook> m_books;
        std::unordered_map<uint16_t, std::string> m_ticker_map;

        void handle_system_event(const uint8_t* data, size_t len);
        void handle_stock_directory(const uint8_t* data, size_t len);
        void handle_add_order(const uint8_t* data, size_t len);
        void handle_delete_order(const uint8_t* data, size_t len);
        void handle_execute_order(const uint8_t* data, size_t len);
        void handle_cancel_order(const uint8_t* data, size_t len);
        void handle_replace_order(const uint8_t* data, size_t len);

};



#endif