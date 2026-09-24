#include <cstdint>

auto mystery_math(std::uint32_t seed) -> std::uint32_t {
    const std::uint32_t grown = seed * 31;
    const std::uint32_t folded = grown ^ (grown >> 5);
    return folded;
}
