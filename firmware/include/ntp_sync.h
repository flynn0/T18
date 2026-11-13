#pragma once

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

class NtpSyncManager {
public:
    NtpSyncManager() : _lastSyncTime(0), _syncIntervalSeconds(86400) {} // 24 hours

    bool syncTime(uint32_t timeoutMs = 10000) {
        Serial.println(F("[NTP] Synchronizing UTC time via pool.ntp.org..."));
        
        // UTC time: 0 timezone offset, 0 daylight offset
        configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

        uint32_t start = millis();
        time_t now = 0;
        struct tm timeinfo;
        
        while ((millis() - start < timeoutMs)) {
            time(&now);
            localtime_r(&now, &timeinfo);
            // Year 2024 is tm_year >= 124
            if (timeinfo.tm_year >= 124) {
                _lastSyncTime = now;
                Serial.printf("[NTP] Time synchronized successfully: %04d-%02d-%02d %02d:%02d:%02d UTC (epoch %ld)\n",
                              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, (long)now);
                return true;
            }
            delay(200);
        }

        Serial.println(F("[NTP] WARNING: Time synchronization timed out"));
        return false;
    }

    bool shouldSync() const {
        time_t now = getCurrentEpoch();
        if (_lastSyncTime == 0 || now < 1700000000) return true;
        return ((uint64_t)now >= ((uint64_t)_lastSyncTime + _syncIntervalSeconds));
    }

    uint64_t getCurrentEpoch() const {
        time_t now;
        time(&now);
        return (uint64_t)now;
    }

private:
    time_t _lastSyncTime;
    uint32_t _syncIntervalSeconds;
};
