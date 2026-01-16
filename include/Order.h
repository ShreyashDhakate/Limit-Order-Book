#pragma once
#include <cstdint>

enum class Side : std::uint8_t {
    Buy,
    Sell
};

enum class OrderType : std::uint8_t {
    GoodTillCancel,
    FillAndKill,
    Market
};

struct Order {
    std::uint64_t order_id = 0;
    std::uint64_t trader_id = 0;
    Side side = Side::Buy;
    std::uint64_t price = 0;
    std::uint64_t quantity = 0;
    OrderType order_type = OrderType::GoodTillCancel;
};

struct OrderModification {
    std::uint64_t order_id;
    std::uint64_t new_price;
    std::uint64_t new_quantity;
    Side new_side;
};
