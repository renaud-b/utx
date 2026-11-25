#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace utx::common {

// IEC units: file sizes are measured in bytes, scaled by 1024.
inline std::string human_readable_bytes(std::uint64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);

    constexpr std::array<const char*, 7> units{
        "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"
    };
    double size = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (size >= 1024.0 && unit + 1 < units.size()) {
        size /= 1024.0;
        ++unit;
    }
    return std::format("{:.1f} {}", size, units[unit]);
}

} // namespace utx::common
