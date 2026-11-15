#pragma once

#include <Arduino.h>

enum class LedMode {
    OFF,
    SOLID_ON,
    BLINKING
};

class LedController {
public:
    LedController(int8_t pin = 5)
        : _pin(pin), _mode(LedMode::OFF), _blinkIntervalMs(250), _lastToggle(0), _state(false) {}

    void setPin(int8_t pin) {
        if (_pin >= 0 && _pin != pin) {
            setOff();
        }
        _pin = pin;
        if (_pin >= 0) {
            pinMode(_pin, OUTPUT);
            setOff();
        }
    }

    void begin() {
        if (_pin >= 0) {
            pinMode(_pin, OUTPUT);
            setOff();
        }
    }

    void setSolid(bool on) {
        _mode = on ? LedMode::SOLID_ON : LedMode::OFF;
        _state = on;
        if (_pin >= 0) {
            digitalWrite(_pin, _state ? HIGH : LOW);
        }
    }

    void setOff() {
        setSolid(false);
    }

    void setBlinkRateHz(float hz) {
        _mode = LedMode::BLINKING;
        if (hz <= 0.0f) {
            _blinkIntervalMs = 500;
        } else {
            _blinkIntervalMs = (uint32_t)(1000.0f / (hz * 2.0f));
        }
    }

    void update() {
        if (_mode == LedMode::BLINKING && _pin >= 0) {
            uint32_t now = millis();
            if (now - _lastToggle >= _blinkIntervalMs) {
                _lastToggle = now;
                _state = !_state;
                digitalWrite(_pin, _state ? HIGH : LOW);
            }
        }
    }

    LedMode getMode() const {
        return _mode;
    }

private:
    int8_t _pin;
    LedMode _mode;
    uint32_t _blinkIntervalMs;
    uint32_t _lastToggle;
    bool _state;
};
