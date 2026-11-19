#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>

// Restart and factory reset are requested from web handlers but executed
// from loop(), after the HTTP response has been sent.
class SystemControl
{
public:
    void requestRestart(uint32_t delayMs = 1500);
    void requestFactoryReset(uint32_t delayMs = 1500);
    void begin();
    void loop();

    // Memory health, sampled once a second.
    uint32_t minFreeHeap() const { return minHeap; }
    // Set when the previous boot ended in a low-memory restart.
    const char *restartNote() const { return note; }

private:
    void watchMemory();
    uint32_t minHeap = UINT32_MAX;
    uint32_t lastSample = 0, lastReport = 0;
    uint16_t lowSeconds = 0;
    const char *note = "";
    bool restartPending = false;
    bool resetPending = false;
    uint32_t at = 0;
};

extern SystemControl systemControl;

#endif
