#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <functional>
#include <algorithm>
#include <chrono>

struct Order {
    uint64_t order_id;
    bool is_buy;
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    void add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity);
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const;
    void print_book(size_t depth = 10) const;

private:
    void match_orders();

    struct Limit {
        std::list<Order*> orders;
        uint64_t total_quantity = 0;
    };

    std::map<double, Limit, std::greater<double>> bids_;
    std::map<double, Limit> asks_;
    std::unordered_map<uint64_t, Order*> order_lookup_;
};
