#include "frame_convert.h"

#include <algorithm>

bool ConvertToLimitedRgba(const uint8_t* source, size_t sourceSize, uint32_t sourceOffset,
                          uint32_t sourceStride, uint32_t sourceWidth, uint32_t sourceHeight,
                          PixelLayout layout, uint32_t maxWidth, uint32_t maxHeight,
                          uint32_t& outputWidth, uint32_t& outputHeight,
                          std::vector<uint8_t>& output)
{
    if (!source || sourceWidth == 0 || sourceHeight == 0 || maxWidth == 0 || maxHeight == 0 ||
        sourceStride < static_cast<uint64_t>(sourceWidth) * 4) {
        return false;
    }
    const uint64_t required = static_cast<uint64_t>(sourceOffset) +
        static_cast<uint64_t>(sourceStride) * (sourceHeight - 1) +
        static_cast<uint64_t>(sourceWidth) * 4;
    if (required > sourceSize) return false;

    uint32_t red = 0;
    uint32_t green = 1;
    uint32_t blue = 2;
    uint32_t alpha = 3;
    bool copyAlpha = false;
    switch (layout) {
    case PixelLayout::Rgba:
        copyAlpha = true;
        break;
    case PixelLayout::Rgbx:
        break;
    case PixelLayout::Bgra:
        red = 2;
        blue = 0;
        copyAlpha = true;
        break;
    case PixelLayout::Bgrx:
        red = 2;
        blue = 0;
        break;
    }

    const double scale = std::min({ 1.0,
        static_cast<double>(maxWidth) / sourceWidth,
        static_cast<double>(maxHeight) / sourceHeight });
    outputWidth = std::max(1u, static_cast<uint32_t>(sourceWidth * scale));
    outputHeight = std::max(1u, static_cast<uint32_t>(sourceHeight * scale));
    output.resize(static_cast<size_t>(outputWidth) * outputHeight * 4);

    source += sourceOffset;
    for (uint32_t y = 0; y < outputHeight; ++y) {
        const uint32_t sourceY = static_cast<uint32_t>(static_cast<uint64_t>(y) * sourceHeight / outputHeight);
        const uint8_t* row = source + static_cast<size_t>(sourceY) * sourceStride;
        for (uint32_t x = 0; x < outputWidth; ++x) {
            const uint32_t sourceX = static_cast<uint32_t>(static_cast<uint64_t>(x) * sourceWidth / outputWidth);
            const uint8_t* pixel = row + static_cast<size_t>(sourceX) * 4;
            uint8_t* target = output.data() + (static_cast<size_t>(y) * outputWidth + x) * 4;
            target[0] = pixel[red];
            target[1] = pixel[green];
            target[2] = pixel[blue];
            target[3] = copyAlpha ? pixel[alpha] : 255;
        }
    }
    return true;
}
