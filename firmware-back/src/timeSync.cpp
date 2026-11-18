#include <coredecls.h> // settimeofday_cb
#include <sys/time.h>

#include "timeSync.h"
#include "configManager.h"

// Any time before this means the clock was never set (it starts at 1970).
static const time_t VALID_AFTER = 1700000000; // Nov 2023

void TimeSync::begin()
{
    settimeofday_cb([this](bool fromSntp) {
        if (fromSntp)
        {
            src = Source::NTP;
            lastSyncMillis = millis();
            Serial.printf_P(PSTR("Time synced via NTP: %s\n"), formatLocal(time(nullptr)).c_str());
        }
    });
    applyConfig();
}

void TimeSync::applyConfig()
{
    appliedRevision = configManager.timeRevision();
    const timeData &t = configManager.timeConfig;
    const char *tz = t.tz[0] ? t.tz : "UTC0";
    if (t.ntpServer[0])
    {
        // Starts SNTP; it resyncs on its own (about once an hour).
        configTime(tz, t.ntpServer);
    }
    else
    {
        setenv("TZ", tz, 1);
        tzset();
    }
}

void TimeSync::loop()
{
    if (appliedRevision != configManager.timeRevision())
        applyConfig();
}

bool TimeSync::valid() const
{
    return time(nullptr) > VALID_AFTER;
}

const char *TimeSync::sourceName() const
{
    switch (src)
    {
    case Source::NTP: return "ntp";
    case Source::Browser: return "browser";
    default: return valid() ? "unknown" : "none";
    }
}

uint32_t TimeSync::lastSyncAgeS() const
{
    return src == Source::None ? UINT32_MAX : (millis() - lastSyncMillis) / 1000;
}

bool TimeSync::setFromBrowser(uint32_t epoch, int offsetMinutes)
{
    if (epoch < VALID_AFTER)
        return false;

    // If no time zone was configured, use the browser's fixed UTC offset.
    // POSIX sign is inverted: UTC+03:30 is written "<+0330>-03:30".
    if (offsetMinutes >= -14 * 60 && offsetMinutes <= 14 * 60)
    {
        int a = abs(offsetMinutes);
        char tz[24];
        snprintf_P(tz, sizeof(tz), PSTR("<%c%02d%02d>%c%02d:%02d"),
                   offsetMinutes >= 0 ? '+' : '-', a / 60, a % 60,
                   offsetMinutes >= 0 ? '-' : '+', a / 60, a % 60);
        configManager.setAutoTimezone(tz);
    }

    // Keep NTP time if it synced within the last 2 hours.
    if (src == Source::NTP && lastSyncAgeS() < 7200)
        return true;

    timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    src = Source::Browser;
    lastSyncMillis = millis();
    Serial.printf_P(PSTR("Time set from browser: %s\n"), formatLocal(time(nullptr)).c_str());
    return true;
}

String TimeSync::formatLocal(time_t t)
{
    tm local;
    localtime_r(&t, &local);
    char buf[32];
    size_t n = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &local);
    // strftime gives +0330; ISO 8601 wants +03:30
    if (n >= 5 && n + 1 < sizeof(buf))
    {
        buf[n + 1] = '\0';
        buf[n] = buf[n - 1];
        buf[n - 1] = buf[n - 2];
        buf[n - 2] = ':';
    }
    return String(buf);
}

TimeSync timeSync;
