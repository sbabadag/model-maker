#pragma once

#include "model_maker/camera.hpp"

#include <windows.h>

namespace mm {

// MODERN VIEWCUBE — sag ustte kameraya kilitli 3B kup.
//   6 yuz  + 12 kenar + 8 kose = 26 yon tiklanabilir; altta ISO/ev dugmesi,
//   solda XYZ triadi. Fare altindaki hedef (yuz/kenar/kose) vurgulanir.
//   Yalnizca KENDI panel dikdortgenine cizer (tuvalin geri kalani korunur) ve
//   arka tampon kullanir (flicker yok).
class ViewCubeRenderer {
public:
    ViewCubeRenderer() = default;
    ~ViewCubeRenderer();

    ViewCubeRenderer(const ViewCubeRenderer&) = delete;
    ViewCubeRenderer& operator=(const ViewCubeRenderer&) = delete;

    // cursor: TUVAL koordinatinda imlec (hover vurgusu icin).
    void draw(HDC target, const RECT& client, const POINT& cursor, const Camera& camera) const;

private:
    HDC ensureBackBuffer(HDC target, int width, int height) const;

    mutable HDC backBufferDc_{};
    mutable HBITMAP backBufferBitmap_{};
    mutable HGDIOBJ backBufferDefaultBitmap_{};
    mutable int backBufferWidth_{};
    mutable int backBufferHeight_{};
    // Etiket/dugme yazisi icin onbellek (her karede CreateFontW pahali).
    mutable HFONT labelFont_{};
};

} // namespace mm
