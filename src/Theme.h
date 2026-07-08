#pragma once
#include <cstdint>

namespace Theme {
    struct ColorRGB {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    // Colors
    inline constexpr ColorRGB COLOR_BG = { 15, 15, 19 };             // #0f0f13
    inline constexpr ColorRGB COLOR_CARD_BG = { 21, 21, 28 };         // #15151c
    inline constexpr ColorRGB COLOR_TEXT = { 245, 246, 248 };         // #f5f6f8
    inline constexpr ColorRGB COLOR_TEXT_DISABLED = { 112, 112, 128 }; // #707080
    inline constexpr ColorRGB COLOR_ACCENT = { 123, 102, 242 };        // #7b66f2
    inline constexpr ColorRGB COLOR_ACCENT_HOVER = { 140, 123, 245 };  // #8c7bf5
    inline constexpr ColorRGB COLOR_BORDER = { 37, 37, 48 };          // #252530
    
    // Status LED Colors
    inline constexpr ColorRGB COLOR_STATUS_CONNECTED = { 77, 245, 110 };    // #4df56e
    inline constexpr ColorRGB COLOR_STATUS_SEARCHING = { 255, 212, 75 };    // #ffd44b
    inline constexpr ColorRGB COLOR_STATUS_DISCONNECTED = { 255, 91, 91 };  // #ff5b5b

    // Layout Dimensions (pixels/points)
    inline constexpr int WINDOW_WIDTH = 780;
    inline constexpr int WINDOW_HEIGHT = 780;
    inline constexpr int PADDING = 30;
    inline constexpr int FRAME_ROUNDING = 12;
}
