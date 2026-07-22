#pragma once

#include <cstdint>

namespace Theme {

struct ColorRGB {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
};

// Цвета интерфейса приложения
inline constexpr ColorRGB COLOR_BG{15, 15, 19};
inline constexpr ColorRGB COLOR_CARD_BG{21, 21, 28};
inline constexpr ColorRGB COLOR_TEXT{245, 246, 248};
inline constexpr ColorRGB COLOR_TEXT_DISABLED{112, 112, 128};
inline constexpr ColorRGB COLOR_ACCENT{123, 102, 242};
inline constexpr ColorRGB COLOR_ACCENT_HOVER{140, 123, 245};
inline constexpr ColorRGB COLOR_BORDER{37, 37, 48};

// Цвета индикатора статуса подключения
inline constexpr ColorRGB COLOR_STATUS_CONNECTED{77, 245, 110};
inline constexpr ColorRGB COLOR_STATUS_SEARCHING{255, 212, 75};
inline constexpr ColorRGB COLOR_STATUS_DISCONNECTED{255, 91, 91};

// Размеры макета и отступы
inline constexpr int WINDOW_WIDTH = 780;
inline constexpr int WINDOW_HEIGHT = 780;
inline constexpr int PADDING = 30;
inline constexpr int FRAME_ROUNDING = 12;

} // namespace Theme


