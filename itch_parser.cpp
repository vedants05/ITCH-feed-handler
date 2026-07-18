#include <iostream>
#include <arpa/inet.h>  

#include "itch_parser.hpp"
#include "itch_messages.hpp"


void ItchParser::process(const uint8_t *data, size_t len) {
    if (len == 0) return;

    switch (data[0]) {
        case 'S': handle_system_event(data, len);    break;
        case 'R': handle_stock_directory(data, len); break;
        case 'A': handle_add_order(data, len);       break;
        case 'D': handle_delete_order(data, len);    break;
        case 'E': handle_execute_order(data, len);   break;
        case 'X': handle_cancel_order(data, len);    break;
        case 'U': handle_replace_order(data, len);   break;
        default:  break;
    }
}

void ItchParser::handle_system_event(const uint8_t *data, size_t len) {
    if (len < sizeof(SystemEventMessage)) return;

    const SystemEventMessage* msg = reinterpret_cast<const SystemEventMessage*>(data);

    if (msg->event_code == 'O')
        std::cout << "[SYSTEM] Market open\n";
    else if (msg->event_code == 'C')
        std::cout << "[SYSTEM] Market close\n";
}

void ItchParser::handle_stock_directory(const uint8_t *data, size_t len) {
       if (len < sizeof(StockDirectoryMessage)) return;

    const StockDirectoryMessage* msg = reinterpret_cast<const StockDirectoryMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    std::string ticker(msg->stock, 8);
    size_t end = ticker.find_last_not_of(' ');
    ticker = ticker.substr(0, end + 1);

    m_ticker_map[locate] = ticker;
    m_books[locate];  
}

void ItchParser::handle_add_order(const uint8_t *data, size_t len) {
    if (len < sizeof(AddOrderMessage)) return;

    const AddOrderMessage* msg = reinterpret_cast<const AddOrderMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    if (m_books.find(locate) == m_books.end()) return;

    uint64_t ref    = betoh64(msg->order_ref_num);
    uint32_t shares = ntohl(msg->shares);
    uint32_t price  = ntohl(msg->price);

    m_books[locate].add_order(ref, msg->buy_sell_indicator, price, shares);
}

void ItchParser::handle_delete_order(const uint8_t *data, size_t len) {
    if (len < sizeof(DeleteOrderMessage)) return;

    const DeleteOrderMessage* msg = reinterpret_cast<const DeleteOrderMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    if (m_books.find(locate) == m_books.end()) return;

    uint64_t ref = betoh64(msg->order_ref_num);

    m_books[locate].delete_order(ref);
}

void ItchParser::handle_execute_order(const uint8_t *data, size_t len) {
    if (len < sizeof(ExecuteOrderMessage)) return;

    const ExecuteOrderMessage* msg = reinterpret_cast<const ExecuteOrderMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    if (m_books.find(locate) == m_books.end()) return;

    uint64_t ref    = betoh64(msg->order_ref_num);
    uint32_t executed_shares = ntohl(msg->executed_shares);

    m_books[locate].execute_order(ref, executed_shares);
}

void ItchParser::handle_cancel_order(const uint8_t *data, size_t len) {
    if (len < sizeof(CancelOrderMessage)) return;

    const CancelOrderMessage* msg = reinterpret_cast<const CancelOrderMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    if (m_books.find(locate) == m_books.end()) return;

    uint64_t ref    = betoh64(msg->order_ref_num);
    uint32_t cancelled_shares = ntohl(msg->cancelled_shares);

    m_books[locate].cancel_order(ref, cancelled_shares);
}

void ItchParser::handle_replace_order(const uint8_t *data, size_t len) {
    if (len < sizeof(ReplaceOrderMessage)) return;

    const ReplaceOrderMessage* msg = reinterpret_cast<const ReplaceOrderMessage*>(data);

    uint16_t locate = ntohs(msg->stock_locate);

    if (m_books.find(locate) == m_books.end()) return;

    uint64_t old_ref = betoh64(msg->original_order_ref_num);
    uint64_t new_ref = betoh64(msg->new_order_ref_num);
    uint32_t shares = ntohl(msg->shares);
    uint32_t price = ntohl(msg->price);

    m_books[locate].replace_order(old_ref, new_ref, shares, price);
}
