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
    void loop();

private:
    bool restartPending = false;
    bool resetPending = false;
    uint32_t at = 0;
};

extern SystemControl systemControl;

#endif
