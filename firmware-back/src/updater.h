#ifndef UPDATER_H
#define UPDATER_H

#include <Arduino.h>

// Flashes a firmware image previously uploaded to LittleFS.
// Status: 255 = idle, 254 = in progress, 1 = success, 0 = failed.
class LittleFSUpdater
{
private:
    String filename;
    bool requestFlag = false;
    uint8_t status = 255;
    void flash(const String &filename);

public:
    void requestStart(const String &filename);
    void loop();
    uint8_t getStatus() const { return status; }
};

extern LittleFSUpdater updater;

#endif
