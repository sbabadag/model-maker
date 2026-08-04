#pragma once

#include "model_maker/camera.hpp"

#include <windows.h>

namespace mm {

class ViewCubeRenderer {
public:
    ViewCubeRenderer() = default;
    ~ViewCubeRenderer();

    ViewCubeRenderer(const ViewCubeRenderer&) = delete;
    ViewCubeRenderer& operator=(const ViewCubeRenderer&) = delete;

    void draw(HDC target, const RECT& client, const POINT& cursor, const Camera& camera) const;

private:
    HDC ensureBackBuffer(HDC target, int width, int height) const;

    mutable HDC backBufferDc_{};
    mutable HBITMAP backBufferBitmap_{};
    mutable HGDIOBJ backBufferDefaultBitmap_{};
    mutable int backBufferWidth_{};
    mutable int backBufferHeight_{};
};

} // namespace mm
