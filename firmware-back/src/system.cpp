#include "system.h"
#include "configManager.h"
#include "logger.h"

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
