#pragma once

// Pin map for ESP32-8048S050C (yellow board) from esp3d.io reference.
// Keep these in one place for easy board swaps.

namespace pins {

// LCD (ST7262, RGB565)
static constexpr int kLcdPclk = 42;
static constexpr int kLcdHsync = 39;
static constexpr int kLcdVsync = 41;
static constexpr int kLcdDe = 40;
static constexpr int kLcdR0 = 45;
static constexpr int kLcdR1 = 48;
static constexpr int kLcdR2 = 47;
static constexpr int kLcdR3 = 21;
static constexpr int kLcdR4 = 14;
static constexpr int kLcdG0 = 5;
static constexpr int kLcdG1 = 6;
static constexpr int kLcdG2 = 7;
static constexpr int kLcdG3 = 15;
static constexpr int kLcdG4 = 16;
static constexpr int kLcdG5 = 4;
static constexpr int kLcdB0 = 8;
static constexpr int kLcdB1 = 3;
static constexpr int kLcdB2 = 46;
static constexpr int kLcdB3 = 9;
static constexpr int kLcdB4 = 1;
static constexpr int kLcdBl = 2;

// Touch (GT911 I2C)
static constexpr int kTouchSda = 19;
static constexpr int kTouchScl = 20;
static constexpr int kTouchInt = -1;
static constexpr int kTouchRst = 38;

} // namespace pins
