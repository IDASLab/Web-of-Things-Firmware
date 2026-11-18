#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include <Arduino.h>
#include "default/config.h"

// Settings are stored as JSON in LittleFS (CONFIG_PATH). Setters only change
// RAM and raise a flag; the file is written from loop(), never from inside a
// web server callback.
#define CONFIG_DIR "/sys"
#define CONFIG_PATH "/sys/config.json"

class config
{
public:
    sensorData sensorConfigData;
    deviceData deviceConfigData;
    brokerData brokerConfigData;
    networkData internal; // station static IP
    adminData admin;

    // Call after LittleFS.begin(). Returns false if defaults had to be used.
    bool begin();
    void loop();

    // Write to flash now (only call from loop context).
    bool save();
    // Restore defaults in RAM and write them to flash now.
    void reset();
    void requestSave() { saveRequested = true; }

    // Setters validate input; on failure they return false and set `error`.
    bool setDeviceConfig(const char *name, const char *ssid, const char *password, String &error);
    bool setStaticDeviceConfig(const char *name, const char *ssid, const char *password,
                               const char *ip, const char *sub, const char *gw, String &error);
    bool setSensorConfig(uint32_t sampleRate, String &error);
    bool setBrokerConfig(bool status, const char *addr, uint16_t port, const char *topic, uint8_t qos, String &error);
    bool setAdminPassword(const char *current, const char *password, String &error);
    void setStationStaticIP(uint32_t ip, uint32_t gw, uint32_t sub, uint32_t dns);

    // Captive portal SSID actually used (falls back to "WoT-<chip id>").
    String portalSSID() const;
    bool adminPasswordIsDefault() const;

    // Bumped on every successful broker change so the publisher can reconnect.
    uint32_t brokerRevision() const { return brokerRev; }

private:
    bool saveRequested = false;
    uint32_t brokerRev = 0;

    void loadDefaults();
    bool load();
    bool importLegacyEEPROM();
};

// Copies src into a fixed buffer; fails (and leaves dst untouched) if src is
// NULL or does not fit.
bool copyField(char *dst, size_t dstSize, const char *src);
bool isValidIP(const char *s);

extern config configManager;

#endif
