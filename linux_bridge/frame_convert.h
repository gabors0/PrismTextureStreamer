#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class PixelLayout
{
    Rgba,
    Rgbx,
    Bgra,
    Bgrx,
};

// Converts a four-byte packed image to RGBA8 and scales it down, preserving its
// aspect ratio, when it exceeds maxWidth or maxHeight. Padding in source rows is
// supported. Returns false when the input bounds or dimensions are invalid.
bool ConvertToLimitedRgba(const uint8_t* source, size_t sourceSize, uint32_t sourceOffset,
                          uint32_t sourceStride, uint32_t sourceWidth, uint32_t sourceHeight,
                          PixelLayout layout, uint32_t maxWidth, uint32_t maxHeight,
                          uint32_t& outputWidth, uint32_t& outputHeight,
                          std::vector<uint8_t>& output);
