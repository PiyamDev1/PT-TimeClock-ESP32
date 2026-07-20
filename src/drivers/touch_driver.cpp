#include "touch_driver.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"
#include "display_driver.h"

namespace ptc {

namespace {

constexpr uint8_t kGt911Addr1 = 0x5D;
constexpr uint8_t kGt911Addr2 = 0x14;
constexpr uint8_t kFt5x06Addr = 0x38;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoints = 0x8150;
constexpr uint16_t kRegResolution = 0x8048;
constexpr uint8_t kFtRegStatus = 0x02;
constexpr uint8_t kFtRegPoint1 = 0x03;
constexpr uint32_t kSampleIntervalMs = 8;
constexpr uint32_t kActiveFallbackIntervalMs = 16;
constexpr uint32_t kIdleFallbackIntervalMs = 250;
constexpr uint32_t kScreenOffFallbackIntervalMs = 50;
constexpr uint16_t kPanelWidth = 800;
constexpr uint16_t kPanelHeight = 480;

enum class TouchController : uint8_t {
    kUnknown,
    kGt911,
    kFt5x06,
};

uint8_t g_addr = kGt911Addr1;
TouchController g_controller = TouchController::kUnknown;
bool g_tap_latched = false;
uint16_t g_raw_width = kPanelWidth;
uint16_t g_raw_height = kPanelHeight;
uint16_t g_sample_x = 0;
uint16_t g_sample_y = 0;
bool g_sample_pressed = false;
bool g_sample_valid = false;
bool g_suppress_until_release = false;
uint32_t g_last_sample_ms = 0;
volatile bool g_irq_pending = true;
volatile bool g_wake_irq_pending = false;

void IRAM_ATTR touch_interrupt_handler() {
    g_irq_pending = true;
    g_wake_irq_pending = true;
}

bool i2c_read(uint16_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(g_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(g_addr, static_cast<uint8_t>(len)) != len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        data[i] = Wire.read();
    }
    return true;
}

bool i2c_write(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(g_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool i2c_read8(uint8_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(g_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(g_addr, static_cast<uint8_t>(len)) != len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        data[i] = Wire.read();
    }
    return true;
}

bool detect_addr() {
    uint8_t buf[1] = {0};
    g_addr = kGt911Addr1;
    if (i2c_read(kRegStatus, buf, 1)) {
        g_controller = TouchController::kGt911;
        return true;
    }
    g_addr = kGt911Addr2;
    if (i2c_read(kRegStatus, buf, 1)) {
        g_controller = TouchController::kGt911;
        return true;
    }
    g_addr = kFt5x06Addr;
    if (i2c_read8(kFtRegStatus, buf, 1)) {
        g_controller = TouchController::kFt5x06;
        return true;
    }
    g_controller = TouchController::kUnknown;
    return false;
}

void detect_resolution() {
    if (g_controller != TouchController::kGt911) {
        return;
    }

    uint8_t data[4] = {0};
    if (!i2c_read(kRegResolution, data, sizeof(data))) {
        return;
    }

    const uint16_t width = static_cast<uint16_t>(data[0] | (data[1] << 8));
    const uint16_t height = static_cast<uint16_t>(data[2] | (data[3] << 8));
    if (width >= 100 && width <= 4096 && height >= 100 && height <= 4096) {
        g_raw_width = width;
        g_raw_height = height;
    }
}

bool read_gt911_point(uint16_t& x, uint16_t& y, bool& pressed) {
    uint8_t status = 0;
    if (!i2c_read(kRegStatus, &status, 1)) {
        pressed = false;
        return false;
    }

    bool data_ready = (status & 0x80) != 0;
    uint8_t points = status & 0x0F;
    if (points == 0) {
        pressed = false;
        if (data_ready) {
            i2c_write(kRegStatus, 0);
        }
        return true;
    }

    uint8_t data[4] = {0};
    if (!i2c_read(kRegPoints, data, sizeof(data))) {
        pressed = false;
        return false;
    }

    x = static_cast<uint16_t>(data[1] << 8 | data[0]);
    y = static_cast<uint16_t>(data[3] << 8 | data[2]);
    pressed = true;
    i2c_write(kRegStatus, 0);
    return true;
}

bool read_ft5x06_point(uint16_t& x, uint16_t& y, bool& pressed) {
    uint8_t status = 0;
    if (!i2c_read8(kFtRegStatus, &status, 1)) {
        pressed = false;
        return false;
    }

    uint8_t points = status & 0x0F;
    if (points == 0) {
        pressed = false;
        return true;
    }

    uint8_t data[4] = {0};
    if (!i2c_read8(kFtRegPoint1, data, sizeof(data))) {
        pressed = false;
        return false;
    }

    x = static_cast<uint16_t>(((data[0] & 0x0F) << 8) | data[1]);
    y = static_cast<uint16_t>(((data[2] & 0x0F) << 8) | data[3]);
    pressed = true;
    return true;
}

bool read_point(uint16_t& x, uint16_t& y, bool& pressed) {
    if (g_controller == TouchController::kFt5x06) {
        return read_ft5x06_point(x, y, pressed);
    }
    if (g_controller == TouchController::kGt911) {
        return read_gt911_point(x, y, pressed);
    }
    pressed = false;
    return false;
}

void apply_rotation(uint16_t& x, uint16_t& y) {
    const uint16_t max_x = kPanelWidth - 1;
    const uint16_t max_y = kPanelHeight - 1;
    switch (lv_disp_get_rotation(lv_disp_get_default())) {
        case LV_DISP_ROT_90: {
            uint16_t new_x = max_y - y;
            uint16_t new_y = x;
            x = new_x;
            y = new_y;
            break;
        }
        case LV_DISP_ROT_180:
            x = max_x - x;
            y = max_y - y;
            break;
        case LV_DISP_ROT_270: {
            uint16_t new_x = y;
            uint16_t new_y = max_x - x;
            x = new_x;
            y = new_y;
            break;
        }
        default:
            break;
    }
}

uint16_t map_axis(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t out_max, bool invert) {
    if (raw_max <= raw_min) {
        return 0;
    }

    if (raw < raw_min) {
        raw = raw_min;
    }
    if (raw > raw_max) {
        raw = raw_max;
    }

    uint32_t span = static_cast<uint32_t>(raw_max - raw_min);
    uint32_t rel = static_cast<uint32_t>(raw - raw_min);
    uint32_t mapped = (rel * out_max) / span;
    if (mapped > out_max) {
        mapped = out_max;
    }

    if (invert) {
        mapped = out_max - mapped;
    }
    return static_cast<uint16_t>(mapped);
}

uint16_t clamp_u16(int32_t value, uint16_t max_value) {
    if (value < 0) {
        return 0;
    }
    if (value > max_value) {
        return max_value;
    }
    return static_cast<uint16_t>(value);
}

} // namespace

bool touch_driver_init() {
    Wire.begin(pins::kTouchSda, pins::kTouchScl);
    Wire.setClock(400000);
    Wire.setTimeOut(10);

    // Holding INT low during reset selects the GT911's 0x5D address and leaves
    // the pin in a known state before it is changed back to an interrupt input.
    if (pins::kTouchInt >= 0) {
        pinMode(pins::kTouchInt, OUTPUT);
        digitalWrite(pins::kTouchInt, LOW);
    }
    if (pins::kTouchRst >= 0) {
        pinMode(pins::kTouchRst, OUTPUT);
        digitalWrite(pins::kTouchRst, LOW);
        delay(10);
        digitalWrite(pins::kTouchRst, HIGH);
        delay(10);
    }
    if (pins::kTouchInt >= 0) {
        pinMode(pins::kTouchInt, INPUT);
    }
    delay(70);

    bool detected = detect_addr();
    if (detected) {
        detect_resolution();
    }
    if (detected && pins::kTouchInt >= 0) {
        attachInterrupt(digitalPinToInterrupt(pins::kTouchInt), touch_interrupt_handler, FALLING);
    }
    Serial.printf("touch_driver_init: detected=%d controller=%s addr=0x%02X raw=%ux%u irq=%d level=%d\n",
        detected ? 1 : 0,
        g_controller == TouchController::kGt911 ? "GT911" : g_controller == TouchController::kFt5x06 ? "FT5x06" : "unknown",
        g_addr,
        static_cast<unsigned>(g_raw_width),
        static_cast<unsigned>(g_raw_height),
        pins::kTouchInt,
        pins::kTouchInt >= 0 ? digitalRead(pins::kTouchInt) : -1);
    return detected;
}

void touch_driver_tick() {
    const uint32_t now = millis();
    const uint32_t elapsed = now - g_last_sample_ms;
    if (g_sample_valid && elapsed < kSampleIntervalMs) {
        return;
    }

    if (pins::kTouchInt >= 0 && !g_irq_pending) {
        const uint32_t fallback_interval = !display_driver_is_backlight_on()
            ? kScreenOffFallbackIntervalMs
            : g_sample_pressed
                ? kActiveFallbackIntervalMs
                : kIdleFallbackIntervalMs;
        if (elapsed < fallback_interval) {
            return;
        }
    }
    g_irq_pending = false;

    uint16_t x = g_sample_x;
    uint16_t y = g_sample_y;
    bool pressed = false;
    g_last_sample_ms = now;
    if (!read_point(x, y, pressed)) {
        return;
    }

    if (g_suppress_until_release && !pressed) {
        g_suppress_until_release = false;
    }
    if (pressed && !g_sample_pressed && !g_suppress_until_release) {
        g_tap_latched = true;
    }
    g_sample_x = x;
    g_sample_y = y;
    g_sample_pressed = pressed;
    g_sample_valid = true;
}

void touch_driver_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    LV_UNUSED(drv);

    if (!g_sample_valid) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    uint16_t x = g_sample_x;
    uint16_t y = g_sample_y;
    const bool pressed = g_sample_pressed;

    x = map_axis(x, 0, static_cast<uint16_t>(g_raw_width - 1), kPanelWidth - 1, false);
    y = map_axis(y, 0, static_cast<uint16_t>(g_raw_height - 1), kPanelHeight - 1, false);

    if (g_suppress_until_release || (pressed && !display_driver_is_backlight_on())) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;

}

bool touch_driver_consume_tap_event() {
    bool tapped = g_tap_latched;
    g_tap_latched = false;
    return tapped;
}

bool touch_driver_consume_wake_event() {
    noInterrupts();
    const bool pending = g_wake_irq_pending;
    g_wake_irq_pending = false;
    interrupts();
    return pending;
}

void touch_driver_prepare_for_screen_off() {
    noInterrupts();
    g_wake_irq_pending = false;
    g_irq_pending = pins::kTouchInt >= 0 && digitalRead(pins::kTouchInt) == LOW;
    interrupts();
    g_tap_latched = false;
}

void touch_driver_suppress_until_release() {
    g_suppress_until_release = true;
    g_tap_latched = false;
}

bool touch_driver_poll_raw(uint16_t& x, uint16_t& y, bool& pressed) {
    if (!g_sample_valid) {
        return false;
    }
    x = g_sample_x;
    y = g_sample_y;
    pressed = g_sample_pressed;
    if (x >= 800) {
        x = 799;
    }
    if (y >= 480) {
        y = 479;
    }
    return true;
}

void touch_driver_set_calibration(const TouchCalibration& calibration) {
    LV_UNUSED(calibration);
}

TouchCalibration touch_driver_get_calibration() {
    return TouchCalibration{};
}

} // namespace ptc
