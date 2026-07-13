#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <cstdint>
#include <map>
#include <unordered_map>

struct Order {
    char     side;   
    uint32_t price;
    uint32_t shares;
};

struct BestBidOffer {
    uint32_t bid_price;
    uint64_t bid_qty;
    uint32_t ask_price;
    uint64_t ask_qty;
};

class OrderBook {

    public: 
        void add_order(uint64_t ref, char side, uint32_t price, uint32_t shares);
        void delete_order(uint64_t ref);
        void execute_order(uint64_t ref, uint32_t executed_shares);
        void cancel_order(uint64_t ref, uint32_t cancelled_shares);
        void replace_order(uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price);
        BestBidOffer best_bid_offer() const;

    private:
        std::map<uint32_t, uint64_t> bids_;
        std::map<uint32_t, uint64_t> asks_;
        std::unordered_map<uint64_t, Order> orders_;
};

#endif