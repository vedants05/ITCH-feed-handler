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
        void deduct_shares(uint32_t price, uint32_t shares, uint64_t ref, std::map<uint32_t, uint64_t> &type);
        void execute_order(uint64_t ref, uint32_t executed_shares);
        void reduce_order(uint64_t ref, uint32_t shares_to_reduce);
        void cancel_order(uint64_t ref, uint32_t cancelled_shares);
        void replace_order(uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price);
        BestBidOffer best_bid_offer() const;

        size_t bid_levels()  const { return m_bids.size(); }
        size_t ask_levels()  const { return m_asks.size(); }
        size_t order_count() const { return m_orders.size(); }

    private:
        std::map<uint32_t, uint64_t> m_bids;
        std::map<uint32_t, uint64_t> m_asks;
        std::unordered_map<uint64_t, Order> m_orders;
};

#endif