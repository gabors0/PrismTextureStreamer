#pragma once

#include <cstdint>

namespace bridge_layout {

struct Rect
{
    uint32_t x{};
    uint32_t y{};
    uint32_t width{};
    uint32_t height{};
};

inline Rect AspectFit(uint32_t sourceWidth, uint32_t sourceHeight,
                      uint32_t destinationWidth, uint32_t destinationHeight)
{
    if (sourceWidth == 0 || sourceHeight == 0 ||
        destinationWidth == 0 || destinationHeight == 0) {
        return {};
    }

    Rect result;
    if (static_cast<uint64_t>(destinationWidth) * sourceHeight >
        static_cast<uint64_t>(destinationHeight) * sourceWidth) {
        result.height = destinationHeight;
        result.width = static_cast<uint32_t>(
            static_cast<uint64_t>(sourceWidth) * destinationHeight / sourceHeight);
        if (result.width == 0) result.width = 1;
    } else {
        result.width = destinationWidth;
        result.height = static_cast<uint32_t>(
            static_cast<uint64_t>(sourceHeight) * destinationWidth / sourceWidth);
        if (result.height == 0) result.height = 1;
    }
    result.x = (destinationWidth - result.width) / 2;
    result.y = (destinationHeight - result.height) / 2;
    return result;
}

} // namespace bridge_layout
