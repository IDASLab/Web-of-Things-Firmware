#include "system.h"
#include "configManager.h"
#include "logger.h"

// Low-memory guard. If free heap stays below LOW_HEAP_BYTES for
// LOW_HEAP_SECONDS, the web server and WiFi can no longer work, so the board
// would sit there unreachable. Restarting is the safer choice for a logger:
// buffered readings are flushed first and the session resumes after boot.
#define LOW_HEAP_BYTES 5000
#define LOW_HEAP_SECONDS 60
#define RTC_NOTE_SLOT 0 // RTC user memory survives a software restart
#define RTC_NOTE_LOW_HEAP 0x4C4F5748UL // "LOWH"

void SystemControl::begin()
{
    uint32_t marker = 0;
    if (ESP.rtcUserMemoryRead(RTC_NOTE_SLOT, &marker, sizeof(marker)) && marker == RTC_NOTE_LOW_HEAP)
        note = "Restarted itself because memory ran low";
    marker = 0;
    ESP.rtcUserMemoryWrite(RTC_NOTE_SLOT, &marker, sizeof(marker));
}

void SystemControl::watchMemory()
{
    if (millis() - lastSample < 1000)
        return;
    lastSample = millis();
    uint32_t heap = ESP.getFreeHeap();
    if (heap < minHeap)
        minHeap = heap;

    // A line every 10 minutes makes a long serial log useful.
    if (millis() - lastReport > 600000UL)
    {
        lastReport = millis();
        Serial.printf_P(PSTR("[health] uptime %lu s, free heap %u, largest block %u, lowest %u\n"),
                        (unsigned long)(millis() / 1000), heap, ESP.getMaxFreeBlockSize(), minHeap);
    }

    lowSeconds = heap < LOW_HEAP_BYTES ? lowSeconds + 1 : 0;
    if (lowSeconds >= LOW_HEAP_SECONDS && !restartPending)
    {
        Serial.printf_P(PSTR("[health] free heap below %u for %u s, restarting\n"), LOW_HEAP_BYTES, LOW_HEAP_SECONDS);
        uint32_t marker = RTC_NOTE_LOW_HEAP;
        ESP.rtcUserMemoryWrite(RTC_NOTE_SLOT, &marker, sizeof(marker));
        requestRestart(0);
    }
}

void SystemControl::requestRestart(uint32_t delayMs)
{
    restartPending = true;
    at = millis() + delayMs;
}

void SystemControl::requestFactoryReset(uint32_t delayMs)
{
    resetPending = true;
    restartPending = true;
    at = millis() + delayMs;
}

void SystemControl::loop()
{
    watchMemory();
    if (!restartPending || (int32_t)(millis() - at) < 0)
        return;

    logger.flushNow(); // the running session continues after the restart

    if (resetPending)
    {
        Serial.println(F("Factory reset: restoring default settings"));
        configManager.reset(); // writes defaults to flash before restarting
    }
    else
    {
        configManager.loop(); // flush any pending settings change
    }
    Serial.println(F("Restarting..."));
    Serial.flush();
    ESP.restart();
}

SystemControl systemControl;
