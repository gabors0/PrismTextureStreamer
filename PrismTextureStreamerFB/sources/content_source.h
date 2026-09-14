#pragma once
#include <cstdint>
#include <vector>
#include <Windows.h>

// A content source produces RGBA8 frames without doing capture work on the render thread.
// Width and height may change between successfully copied frames.
class IContentSource
{
public:
    virtual ~IContentSource() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    virtual void SetFramerate(uint8_t framerate) = 0;
    virtual bool CopyLatestFrame(std::vector<uint8_t>& dst) = 0;
};
