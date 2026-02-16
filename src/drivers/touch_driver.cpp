#include "touch_driver.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace ptc {

namespace {

constexpr uint8_t kGt911Addr1 = 0x5D;
constexpr uint8_t kGt911Addr2 = 0x14;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoints = 0x8150;

uint8_t g_addr = kGt911Addr1;

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

    uint8_t points = status & 0x0F;
    if (points == 0) {
        pressed = false;
        i2c_write(kRegStatus, 0);
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

} // namespace

bool touch_driver_init() {
    Wire.begin(pins::kTouchSda, pins::kTouchScl);
    pinMode(pins::kTouchRst, OUTPUT);
    digitalWrite(pins::kTouchRst, LOW);
    delay(10);
    digitalWrite(pins::kTouchRst, HIGH);
    delay(50);

    return detect_addr();
}

void touch_driver_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    LV_UNUSED(drv);

    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    if (!read_point(x, y, pressed)) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    apply_rotation(x, y);
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;
    return;
}

} // namespace ptc
