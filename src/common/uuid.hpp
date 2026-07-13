#pragma once

/// @file uuid.hpp
/// @brief Lightweight UUID v4 generator.

#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace prodxcloud {

inline std::string generate_uuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t hi = dist(rng);
    uint64_t lo = dist(rng);

    // Version 4 + variant bits
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    auto hex = [](std::ostringstream& os, uint64_t val, int nibbles, int start) {
        for (int i = start; i < start + nibbles; ++i) {
            int shift = (15 - i) * 4;
            os << std::hex << ((val >> shift) & 0xF);
        }
    };

    std::ostringstream os;
    hex(os, hi, 8, 0);  os << '-';
    hex(os, hi, 4, 8);  os << '-';
    hex(os, hi, 4, 12); os << '-';
    hex(os, lo, 4, 0);  os << '-';
    hex(os, lo, 12, 4);
    return os.str();
}

}  // namespace prodxcloud
