#include "order_book.h"
#include <iostream>
#include <chrono>

uint64_t get_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

void test_add_orders(OrderBook& book) {
    std::cout << "\n--- Testing Add Orders ---\n";
    book.add_order({1, true, 100.50, 10, get_timestamp_ns()});
    book.add_order({2, true, 100.50, 15, get_timestamp_ns()});
    book.add_order({3, true, 101.00, 5, get_timestamp_ns()});
    book.add_order({4, false, 102.00, 20, get_timestamp_ns()});
    book.add_order({5, false, 102.50, 10, get_timestamp_ns()});
    book.add_order({6, false, 102.00, 5, get_timestamp_ns()});
    book.print_book();
}

void test_cancel_orders(OrderBook& book) {
    std::cout << "\n--- Testing Cancel Orders ---\n";
    book.cancel_order(2); // Cancel a buy order
    book.print_book();
    
    book.cancel_order(6); // Cancel a sell order
    book.print_book();
    
    book.cancel_order(99); // Try to cancel non-existent order
}

void test_amend_orders(OrderBook& book) {
    std::cout << "\n--- Testing Amend Orders ---\n";
    // Amend quantity only
    book.amend_order(1, 100.50, 12);
    book.print_book();

    // Amend price
    book.amend_order(3, 100.75, 7);
    book.print_book();
    
    // Amend a sell order
    book.amend_order(4, 101.50, 25);
    book.print_book();

    book.amend_order(99, 100.0, 100); // Try to amend non-existent order
}

void test_matching() {
    std::cout << "\n\n--- Testing Matching Logic ---\n";
    OrderBook matching_book;

    std::cout << "\n--- Scenario 1: Simple match (full fill) ---\n";
    matching_book.add_order({101, true, 100.0, 10, get_timestamp_ns()});
    matching_book.print_book();
    matching_book.add_order({102, false, 100.0, 10, get_timestamp_ns()});
    matching_book.print_book();

    std::cout << "\n--- Scenario 2: Partial fill ---\n";
    matching_book.add_order({201, true, 101.0, 20, get_timestamp_ns()});
    matching_book.print_book();
    matching_book.add_order({202, false, 101.0, 15, get_timestamp_ns()});
    matching_book.print_book();

    std::cout << "\n--- Scenario 3: Market order crossing the spread ---\n";
    matching_book.add_order({301, false, 102.0, 10, get_timestamp_ns()});
    matching_book.print_book();
    matching_book.add_order({302, true, 103.0, 5, get_timestamp_ns()});
    matching_book.print_book();

    std::cout << "\n--- Scenario 4: Matching multiple levels ---\n";
    matching_book.add_order({401, false, 104.0, 10, get_timestamp_ns()});
    matching_book.add_order({402, false, 105.0, 10, get_timestamp_ns()});
    matching_book.print_book();
    matching_book.add_order({403, true, 105.0, 25, get_timestamp_ns()});
    matching_book.print_book();
}

int main() {
    OrderBook book;

    test_add_orders(book);
    test_cancel_orders(book);
    test_amend_orders(book);
    
    std::cout << "\n--- Final Order Book State ---\n";
    book.print_book(5);

    test_matching();

    return 0;
}
