#include "order_book.h"

OrderBook::OrderBook() {
    std::cout << "OrderBook created." << std::endl;
}

OrderBook::~OrderBook() {
    std::cout << "Destroying OrderBook. Clearing all orders." << std::endl;
    for (auto const& [order_id, order_ptr] : order_lookup_) {
        delete order_ptr;
    }
    order_lookup_.clear();
}

void OrderBook::add_order(const Order& order) {
    if (order_lookup_.count(order.order_id)) {
        std::cerr << "Error: Order with ID " << order.order_id << " already exists." << std::endl;
        return;
    }

    Order* new_order = new Order(order);
    order_lookup_[order.order_id] = new_order;

    std::cout << "Adding order " << order.order_id << ": " << (order.is_buy ? "BUY" : "SELL")
              << " " << order.quantity << " @ " << order.price << std::endl;

    if (order.is_buy) {
        bids_[order.price].orders.push_back(new_order);
        bids_[order.price].total_quantity += order.quantity;
    } else {
        asks_[order.price].orders.push_back(new_order);
        asks_[order.price].total_quantity += order.quantity;
    }

    match_orders();
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        std::cerr << "Error: Cannot cancel. Order with ID " << order_id << " not found." << std::endl;
        return false;
    }

    Order* order_to_cancel = it->second;
    std::cout << "Cancelling order " << order_id << std::endl;

    if (order_to_cancel->is_buy) {
        auto price_it = bids_.find(order_to_cancel->price);
        if (price_it != bids_.end()) {
            auto& limit = price_it->second;
            limit.total_quantity -= order_to_cancel->quantity;
            limit.orders.remove_if([order_id](Order* o) { return o->order_id == order_id; });
            if (limit.orders.empty()) {
                bids_.erase(price_it);
            }
        }
    } else {
        auto price_it = asks_.find(order_to_cancel->price);
        if (price_it != asks_.end()) {
            auto& limit = price_it->second;
            limit.total_quantity -= order_to_cancel->quantity;
            limit.orders.remove_if([order_id](Order* o) { return o->order_id == order_id; });
            if (limit.orders.empty()) {
                asks_.erase(price_it);
            }
        }
    }

    delete order_to_cancel;
    order_lookup_.erase(it);
    return true;
}

bool OrderBook::amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        std::cerr << "Error: Cannot amend. Order with ID " << order_id << " not found." << std::endl;
        return false;
    }

    Order* order_to_amend = it->second;
    std::cout << "Amending order " << order_id << ": New Price=" << new_price << ", New Quantity=" << new_quantity << std::endl;
    
    if (order_to_amend->price != new_price) {
        Order amended_order = *order_to_amend;
        amended_order.price = new_price;
        amended_order.quantity = new_quantity;
        amended_order.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        order_lookup_.erase(it);
        cancel_order(order_id);
        add_order(amended_order);
    } else if (order_to_amend->quantity != new_quantity) {
        if (order_to_amend->is_buy) {
            auto price_it = bids_.find(order_to_amend->price);
            if (price_it != bids_.end()) {
                price_it->second.total_quantity -= order_to_amend->quantity;
                price_it->second.total_quantity += new_quantity;
                order_to_amend->quantity = new_quantity;
            }
        } else {
            auto price_it = asks_.find(order_to_amend->price);
            if (price_it != asks_.end()) {
                price_it->second.total_quantity -= order_to_amend->quantity;
                price_it->second.total_quantity += new_quantity;
                order_to_amend->quantity = new_quantity;
            }
        }
    }
    return true;
}

void OrderBook::get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
    bids.clear();
    asks.clear();

    size_t count = 0;
    for (const auto& [price, limit] : bids_) {
        if (count++ >= depth) break;
        bids.push_back({price, limit.total_quantity});
    }

    count = 0;
    for (const auto& [price, limit] : asks_) {
        if (count++ >= depth) break;
        asks.push_back({price, limit.total_quantity});
    }
}

void OrderBook::print_book(size_t depth) const {
    std::vector<PriceLevel> bids_snapshot, asks_snapshot;
    get_snapshot(depth, bids_snapshot, asks_snapshot);

    std::cout << "\n--- Order Book ---\n";
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "       ASKS\n";
    std::cout << "---------------------\n";
    std::cout << "Price   | Quantity\n";
    std::cout << "---------------------\n";
    for (int i = asks_snapshot.size() - 1; i >= 0; --i) {
        std::cout << std::setw(8) << asks_snapshot[i].price << "| " << asks_snapshot[i].total_quantity << std::endl;
    }

    std::cout << "\n       BIDS\n";
    std::cout << "---------------------\n";
    std::cout << "Price   | Quantity\n";
    std::cout << "---------------------\n";
    for (const auto& level : bids_snapshot) {
        std::cout << std::setw(8) << level.price << "| " << level.total_quantity << std::endl;
    }
    std::cout << "---------------------\n\n";
}

void OrderBook::match_orders() {
    while (!bids_.empty() && !asks_.empty()) {
        auto best_bid_it = bids_.begin();
        auto best_ask_it = asks_.begin();

        if (best_bid_it->first < best_ask_it->first) {
            break; // No more matches
        }

        std::cout << "\n--- Match Found! ---\n";
        std::cout << "Best Bid: " << best_bid_it->first << " | Best Ask: " << best_ask_it->first << std::endl;

        Limit& bid_limit = best_bid_it->second;
        Limit& ask_limit = best_ask_it->second;

        Order* bid_order = bid_limit.orders.front();
        Order* ask_order = ask_limit.orders.front();

        uint64_t trade_quantity = std::min(bid_order->quantity, ask_order->quantity);
        double trade_price = (bid_order->timestamp_ns < ask_order->timestamp_ns) ? bid_order->price : ask_order->price;

        std::cout << "Executing Trade: " << trade_quantity << " units at " << trade_price << std::endl;

        bid_order->quantity -= trade_quantity;
        ask_order->quantity -= trade_quantity;
        bid_limit.total_quantity -= trade_quantity;
        ask_limit.total_quantity -= trade_quantity;

        if (bid_order->quantity == 0) {
            std::cout << "Bid order " << bid_order->order_id << " fully filled." << std::endl;
            bid_limit.orders.pop_front();
            order_lookup_.erase(bid_order->order_id);
            delete bid_order;
        }

        if (ask_order->quantity == 0) {
            std::cout << "Ask order " << ask_order->order_id << " fully filled." << std::endl;
            ask_limit.orders.pop_front();
            order_lookup_.erase(ask_order->order_id);
            delete ask_order;
        }

        if (bid_limit.orders.empty()) {
            bids_.erase(best_bid_it);
        }
        if (ask_limit.orders.empty()) {
            asks_.erase(best_ask_it);
        }
    }
}
