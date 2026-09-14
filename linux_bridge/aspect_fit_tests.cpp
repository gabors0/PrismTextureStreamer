#include "../bridge/aspect_fit.h"

#include <cassert>
#include <iostream>

int main()
{
    using bridge_layout::AspectFit;

    const auto pillarbox = AspectFit(4, 3, 1920, 1080);
    assert(pillarbox.x == 240 && pillarbox.y == 0);
    assert(pillarbox.width == 1440 && pillarbox.height == 1080);

    const auto letterbox = AspectFit(16, 9, 1024, 1024);
    assert(letterbox.x == 0 && letterbox.y == 224);
    assert(letterbox.width == 1024 && letterbox.height == 576);

    const auto exact = AspectFit(16, 9, 1280, 720);
    assert(exact.x == 0 && exact.y == 0);
    assert(exact.width == 1280 && exact.height == 720);

    const auto empty = AspectFit(0, 9, 1280, 720);
    assert(empty.width == 0 && empty.height == 0);

    std::cout << "All aspect-fit tests passed\n";
    return 0;
}
