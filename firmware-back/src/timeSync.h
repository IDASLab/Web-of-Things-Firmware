#ifndef TIMESYNC_H
#define TIMESYNC_H

#include <Arduino.h>
#include <time.h>

// Wall-clock time for log timestamps.
//  - NTP (with the configured POSIX time zone) when the device has internet.
//  - Otherwise the logger page sends the browser's time (POST /api/time/set),
//    which is good enough for a lab without internet access.
class TimeSync
{
public:
    enum class Source : uint8_t { None, Browser, NTP };

    void begin();
    void loop();

    bool valid() const;                 // true once the clock has been set
    time_t now() const { return time(nullptr); }
    Source source() const { return src; }
    const char *sourceName() const;
    uint32_t lastSyncAgeS() const;       // seconds since the last sync

    // From the browser: unix time in seconds and the browser's UTC offset in
    // minutes (as JavaScript's -getTimezoneOffset()). NTP time wins if recent.
    bool setFromBrowser(uint32_t epoch, int offsetMinutes);

    // ISO 8601 local time with offset, e.g. 2026-11-18T09:30:05+03:30
    static String formatLocal(time_t t);

private:
    Source src = Source::None;
    uint32_t lastSyncMillis = 0;
    uint32_t appliedRevision = UINT32_MAX;

    void applyConfig();
};

extern TimeSync timeSync;

#endif
