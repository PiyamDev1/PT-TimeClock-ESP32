#include "touch_driver.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"
#include "display_driver.h"

namespace ptc {

namespace {

constexpr uint8_t kGt911Addr1 = 0x5D;
constexpr uint8_t kGt911Addr2 = 0x14;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoints = 0x8150;
constexpr bool kTouchCalibrationEnabled = false;

uint8_t g_addr = kGt911Addr1;
bool g_tap_latched = false;
TouchCalibration g_calibration;
uint32_t g_last_touch_log_ms = 0;

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

bool detect_addr() {
    uint8_t buf[1] = {0};
    g_addr = kGt911Addr1;
    if (i2c_read(kRegStatus, buf, 1)) {
        return true;
    }
    g_addr = kGt911Addr2;
    return i2c_read(kRegStatus, buf, 1);
}

bool read_point(uint16_t& x, uint16_t& y, bool& pressed) {
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

void apply_rotation(uint16_t& x, uint16_t& y) {
    const uint16_t max_x = 800 - 1;
    const uint16_t max_y = 480 - 1;
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
    Wire.setClock(100000);
    Wire.setTimeOut(25);
    if (pins::kTouchRst >= 0) {
        pinMode(pins::kTouchRst, OUTPUT);
        digitalWrite(pins::kTouchRst, LOW);
        delay(10);
        digitalWrite(pins::kTouchRst, HIGH);
        delay(80);
    }

    bool detected = detect_addr();
    Serial.printf("touch_driver_init: detected=%d addr=0x%02X\n", detected ? 1 : 0, g_addr);
    return detected;
}

void touch_driver_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    LV_UNUSED(drv);

    lv_disp_t* disp = lv_disp_get_default();
    uint16_t out_w = disp ? static_cast<uint16_t>(lv_disp_get_hor_res(disp)) : 480;
    uint16_t out_h = disp ? static_cast<uint16_t>(lv_disp_get_ver_res(disp)) : 800;
    if (out_w == 0) {
        out_w = 1;
    }
    if (out_h == 0) {
        out_h = 1;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    if (!read_point(x, y, pressed)) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (kTouchCalibrationEnabled && g_calibration.valid) {
        int32_t mapped_x = 0;
        int32_t mapped_y = 0;
        if (g_calibration.use_affine) {
            float raw_x = static_cast<float>(x);
            float raw_y = static_cast<float>(y);
            mapped_x = static_cast<int32_t>(
                g_calibration.affine_xx * raw_x +
                g_calibration.affine_xy * raw_y +
                g_calibration.affine_x0);
            mapped_y = static_cast<int32_t>(
                g_calibration.affine_yx * raw_x +
                g_calibration.affine_yy * raw_y +
                g_calibration.affine_y0);
        } else {
            uint16_t source_x = g_calibration.swap_xy ? y : x;
            uint16_t source_y = g_calibration.swap_xy ? x : y;
            mapped_x = static_cast<int32_t>(g_calibration.scale_x * static_cast<float>(source_x) + g_calibration.offset_x);
            mapped_y = static_cast<int32_t>(g_calibration.scale_y * static_cast<float>(source_y) + g_calibration.offset_y);
        }

        x = clamp_u16(mapped_x, static_cast<uint16_t>(out_w - 1));
        y = clamp_u16(mapped_y, static_cast<uint16_t>(out_h - 1));
    } else {
        bool portrait_ui = out_h > out_w;
        if (portrait_ui) {
            bool raw_looks_landscape = x < 800 && y < 480;
            if (raw_looks_landscape) {
                apply_rotation(x, y);
            }
        }
        if (x >= out_w) {
            x = static_cast<uint16_t>(out_w - 1);
        }
        if (y >= out_h) {
            y = static_cast<uint16_t>(out_h - 1);
        }
    }

    if (pressed) {
        g_tap_latched = true;
    }

    if (pressed && (!display_driver_is_backlight_on() || display_driver_is_backlight_dimmed())) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;

    if (pressed) {
        uint32_t now = millis();
        if (now - g_last_touch_log_ms > 150) {
            g_last_touch_log_ms = now;
            Serial.printf("[TOUCH] x=%u y=%u state=PR cal=%d\n",
                static_cast<unsigned>(x),
                static_cast<unsigned>(y),
                g_calibration.valid ? 1 : 0);
        }
    }

    return;
}

bool touch_driver_consume_tap_event() {
    bool tapped = g_tap_latched;
    g_tap_latched = false;
    return tapped;
}

bool touch_driver_poll_raw(uint16_t& x, uint16_t& y, bool& pressed) {
    bool ok = read_point(x, y, pressed);
    if (!ok) {
        return false;
    }
    if (x >= 800) {
        x = 799;
    }
    if (y >= 480) {
        y = 479;
    }
    return true;
}

void touch_driver_set_calibration(const TouchCalibration& calibration) {
    if (kTouchCalibrationEnabled) {
        g_calibration = calibration;
        return;
    }
    g_calibration = TouchCalibration{};
    g_calibration.valid = false;
}

TouchCalibration touch_driver_get_calibration() {
    return g_calibration;
}

} // namespace ptc
