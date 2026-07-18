#include "order_book.hpp"

void OrderBook::add_order(uint64_t ref, char side, uint32_t price, uint32_t shares) {
    Order order = Order{side, price, shares};
    m_orders[ref] = order;

    if (side == 'B')
    {
        m_bids[price] += shares;
    }
    else
    {
        m_asks[price] += shares;
    }
}

void OrderBook::delete_order(uint64_t ref) {

    if (!m_orders.count(ref))
        return;

    const Order &order = m_orders[ref];

    if (order.side == 'B')
        deduct_shares(order.price, order.shares, ref, m_bids);

    else
        deduct_shares(order.price, order.shares, ref, m_asks);

    m_orders.erase(ref);
}

void OrderBook::deduct_shares(uint32_t price, uint32_t shares, uint64_t ref, std::map<uint32_t, uint64_t> &type) {
    type[price] -= shares;

    if (type[price] == 0)
        type.erase(price);
}

void OrderBook::execute_order(uint64_t ref, uint32_t executed_shares) {
    reduce_order(ref, executed_shares);
}

void OrderBook::cancel_order(uint64_t ref, uint32_t cancelled_shares) {
    reduce_order(ref, cancelled_shares);
}

void OrderBook::reduce_order(uint64_t ref, uint32_t shares_to_reduce) {
    if (!m_orders.count(ref))
        return;

    Order &order = m_orders[ref];

    if (order.side == 'B')
        deduct_shares(order.price, shares_to_reduce, ref, m_bids);
    else
        deduct_shares(order.price, shares_to_reduce, ref, m_asks);

    order.shares -= shares_to_reduce;

    if (order.shares == 0)
        m_orders.erase(ref);
}

void OrderBook::replace_order(uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price) {

    if (!m_orders.count(old_ref))
        return;

    Order old_order = m_orders[old_ref];

    char old_side = old_order.side;
    delete_order(old_ref);
    add_order(new_ref, old_side, price, shares);
}

BestBidOffer OrderBook::best_bid_offer() const {

    BestBidOffer best_prices = {0 ,0 ,0 ,0 };

    if (!(m_bids.empty())) {
        auto best_bid = m_bids.rbegin();
        best_prices.bid_price = best_bid->first;  
        best_prices.bid_qty   = best_bid->second; 
    }

    if (!(m_asks.empty())) {
        auto best_ask = m_asks.begin();
        best_prices.ask_price = best_ask->first;  
        best_prices.ask_qty   = best_ask->second; 
    }

    return best_prices;
}
