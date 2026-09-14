#include "frame_convert.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    {
        const uint8_t bgra[] = { 30, 20, 10, 40, 3, 2, 1, 4 };
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> output;
        assert(ConvertToLimitedRgba(bgra, sizeof(bgra), 0, 8, 2, 1, PixelLayout::Bgra,
                                    1280, 720, width, height, output));
        assert(width == 2 && height == 1);
        assert((output == std::vector<uint8_t>{ 10, 20, 30, 40, 1, 2, 3, 4 }));
    }
    {
        // Two source rows with four padding bytes; downscale 2x2 to 1x1.
        const uint8_t rgbx[] = {
            1, 2, 3, 0, 4, 5, 6, 0, 99, 99, 99, 99,
            7, 8, 9, 0, 10, 11, 12, 0, 99, 99, 99, 99,
        };
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> output;
        assert(ConvertToLimitedRgba(rgbx, sizeof(rgbx), 0, 12, 2, 2, PixelLayout::Rgbx,
                                    1, 1, width, height, output));
        assert(width == 1 && height == 1);
        assert((output == std::vector<uint8_t>{ 1, 2, 3, 255 }));
        assert(!ConvertToLimitedRgba(rgbx, 7, 0, 8, 2, 1, PixelLayout::Rgba,
                                     1, 1, width, height, output));
    }

    std::cout << "All frame conversion tests passed\n";
    return 0;
}
