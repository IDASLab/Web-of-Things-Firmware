#include <Arduino.h>
#include <EEPROM.h>
#include <IPAddress.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "configManager.h"

#define CONFIG_TMP_PATH "/sys/config.tmp"

bool copyField(char *dst, size_t dstSize, const char *src)
{
    if (src == nullptr)
        return false;
    size_t len = strlen(src);
    if (len >= dstSize)
        return false;
    memcpy(dst, src, len + 1);
    return true;
}

bool isValidIP(const char *s)
{
    IPAddress ip;
    return s != nullptr && ip.fromString(s);
}

void config::loadDefaults()
{
    memcpy(&deviceConfigData, &deviceDefaults, sizeof(deviceConfigData));
    memcpy(&sensorConfigData, &sensorDefaults, sizeof(sensorConfigData));
    memcpy(&brokerConfigData, &brokerDefaults, sizeof(brokerConfigData));
    memcpy(&admin, &adminDefaults, sizeof(admin));
    memcpy(&timeConfig, &timeDefaults, sizeof(timeConfig));
    internal = networkData();
}

bool config::begin()
{
    loadDefaults();

    if (load())
        return true;

    if (importLegacyEEPROM())
    {
        Serial.println(F("Imported settings from previous firmware (EEPROM)"));
        save();
        return true;
    }

    Serial.println(F("No valid config found, using defaults"));
    save();
    return false;
}

static void readStr(JsonVariantConst v, char *dst, size_t size)
{
    const char *s = v.as<const char *>();
    if (s != nullptr)
        copyField(dst, size, s); // ignore values that do not fit
}

bool config::load()
{
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f)
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        Serial.printf_P(PSTR("Config file corrupt (%s)\n"), err.c_str());
        return false;
    }

    // Missing keys keep their defaults, so new fields can be added later.
    JsonObjectConst d = doc["device"];
    readStr(d["name"], deviceConfigData.deviceName, sizeof(deviceConfigData.deviceName));
    readStr(d["apSSID"], deviceConfigData.deviceSSID, sizeof(deviceConfigData.deviceSSID));
    readStr(d["apPass"], deviceConfigData.devicePass, sizeof(deviceConfigData.devicePass));
    readStr(d["apIP"], deviceConfigData.deviceIP, sizeof(deviceConfigData.deviceIP));
    readStr(d["apGw"], deviceConfigData.deviceGw, sizeof(deviceConfigData.deviceGw));
    readStr(d["apSub"], deviceConfigData.deviceSub, sizeof(deviceConfigData.deviceSub));

    JsonObjectConst n = doc["network"];
    internal.ip = n["ip"] | 0u;
    internal.gw = n["gw"] | 0u;
    internal.sub = n["sub"] | 0u;
    internal.dns = n["dns"] | 0u;

    JsonObjectConst s = doc["sensor"];
    sensorConfigData.model = s["model"] | sensorDefaults.model;
    sensorConfigData.gpio = s["gpio"] | sensorDefaults.gpio;
    sensorConfigData.sampleRate = constrain(s["sampleRate"] | sensorDefaults.sampleRate, SAMPLE_RATE_MIN_MS, SAMPLE_RATE_MAX_MS);
    sensorConfigData.tempOffset = constrain(s["tempOffset"] | 0.0f, -TEMP_OFFSET_MAX, TEMP_OFFSET_MAX);
    sensorConfigData.humOffset = constrain(s["humOffset"] | 0.0f, -HUM_OFFSET_MAX, HUM_OFFSET_MAX);

    JsonObjectConst t = doc["time"];
    if (t["ntp"].is<const char *>())
        readStr(t["ntp"], timeConfig.ntpServer, sizeof(timeConfig.ntpServer));
    readStr(t["tz"], timeConfig.tz, sizeof(timeConfig.tz));

    JsonObjectConst b = doc["broker"];
    brokerConfigData.publishRequest = b["enabled"] | false;
    readStr(b["host"], brokerConfigData.address, sizeof(brokerConfigData.address));
    brokerConfigData.port = b["port"] | brokerDefaults.port;
    readStr(b["topic"], brokerConfigData.topic, sizeof(brokerConfigData.topic));
    brokerConfigData.qos = min<uint8_t>(b["qos"] | 0, 2);

    readStr(doc["admin"]["password"], admin.password, sizeof(admin.password));
    return true;
}

bool config::save()
{
    saveRequested = false;

    JsonDocument doc;
    JsonObject d = doc["device"].to<JsonObject>();
    d["name"] = deviceConfigData.deviceName;
    d["apSSID"] = deviceConfigData.deviceSSID;
    d["apPass"] = deviceConfigData.devicePass;
    d["apIP"] = deviceConfigData.deviceIP;
    d["apGw"] = deviceConfigData.deviceGw;
    d["apSub"] = deviceConfigData.deviceSub;

    JsonObject n = doc["network"].to<JsonObject>();
    n["ip"] = internal.ip;
    n["gw"] = internal.gw;
    n["sub"] = internal.sub;
    n["dns"] = internal.dns;

    JsonObject s = doc["sensor"].to<JsonObject>();
    s["model"] = sensorConfigData.model;
    s["gpio"] = sensorConfigData.gpio;
    s["sampleRate"] = sensorConfigData.sampleRate;
    s["tempOffset"] = sensorConfigData.tempOffset;
    s["humOffset"] = sensorConfigData.humOffset;

    JsonObject t = doc["time"].to<JsonObject>();
    t["ntp"] = timeConfig.ntpServer;
    t["tz"] = timeConfig.tz;

    JsonObject b = doc["broker"].to<JsonObject>();
    b["enabled"] = brokerConfigData.publishRequest;
    b["host"] = brokerConfigData.address;
    b["port"] = brokerConfigData.port;
    b["topic"] = brokerConfigData.topic;
    b["qos"] = brokerConfigData.qos;

    doc["admin"]["password"] = admin.password;

    // Write to a temp file, then rename, so a power cut never leaves a
    // half-written config behind.
    LittleFS.mkdir(CONFIG_DIR);
    File f = LittleFS.open(CONFIG_TMP_PATH, "w");
    if (!f)
    {
        Serial.println(F("Config save failed: cannot open file"));
        return false;
    }
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0)
    {
        Serial.println(F("Config save failed: write error"));
        LittleFS.remove(CONFIG_TMP_PATH);
        return false;
    }
    LittleFS.remove(CONFIG_PATH);
    if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH))
    {
        Serial.println(F("Config save failed: rename error"));
        return false;
    }
    Serial.println(F("Config saved"));
    return true;
}

void config::reset()
{
    loadDefaults();
    brokerRev++;
    timeRev++;
    save();
}

void config::loop()
{
    if (saveRequested)
        save();
}

static bool validAPPassword(const char *p, String &error)
{
    size_t len = strlen(p);
    if (len < AP_PASS_MIN || len > AP_PASS_MAX)
    {
        error = F("Access point password must be 8 to 63 characters");
        return false;
    }
    return true;
}

bool config::setDeviceConfig(const char *name, const char *ssid, const char *password, String &error)
{
    deviceData next = deviceConfigData;
    if (!copyField(next.deviceName, sizeof(next.deviceName), name) || strlen(name) == 0)
    {
        error = F("Device name must be 1 to 32 characters");
        return false;
    }
    if (!copyField(next.deviceSSID, sizeof(next.deviceSSID), ssid))
    {
        error = F("Access point SSID must be at most 32 characters");
        return false;
    }
    // An empty password keeps the current one (the UI never receives it).
    if (password != nullptr && strlen(password) > 0)
    {
        if (!validAPPassword(password, error))
            return false;
        copyField(next.devicePass, sizeof(next.devicePass), password);
    }
    deviceConfigData = next;
    saveRequested = true;
    return true;
}

bool config::setStaticDeviceConfig(const char *name, const char *ssid, const char *password,
                                   const char *ip, const char *sub, const char *gw, String &error)
{
    if (!isValidIP(ip) || !isValidIP(sub) || !isValidIP(gw))
    {
        error = F("Invalid IP, subnet or gateway address");
        return false;
    }
    if (!setDeviceConfig(name, ssid, password, error))
        return false;
    copyField(deviceConfigData.deviceIP, sizeof(deviceConfigData.deviceIP), ip);
    copyField(deviceConfigData.deviceSub, sizeof(deviceConfigData.deviceSub), sub);
    copyField(deviceConfigData.deviceGw, sizeof(deviceConfigData.deviceGw), gw);
    saveRequested = true;
    return true;
}

bool config::setSensorConfig(uint32_t sampleRate, String &error)
{
    if (sampleRate < SAMPLE_RATE_MIN_MS || sampleRate > SAMPLE_RATE_MAX_MS)
    {
        error = F("Sample rate must be between 2000 and 3600000 ms");
        return false;
    }
    sensorConfigData.sampleRate = sampleRate;
    saveRequested = true;
    return true;
}

bool config::setBrokerConfig(bool status, const char *addr, uint16_t port, const char *topic, uint8_t qos, String &error)
{
    brokerData next = brokerConfigData;
    next.publishRequest = status;
    if (!copyField(next.address, sizeof(next.address), addr ? addr : ""))
    {
        error = F("Broker address must be at most 64 characters");
        return false;
    }
    if (status && strlen(next.address) == 0)
    {
        error = F("Broker address is required");
        return false;
    }
    if (status && port == 0)
    {
        error = F("Broker port is required");
        return false;
    }
    if (!copyField(next.topic, sizeof(next.topic), topic ? topic : "") || (status && strlen(next.topic) == 0))
    {
        error = F("Topic must be 1 to 64 characters");
        return false;
    }
    if (qos > 2)
    {
        error = F("QoS must be 0, 1 or 2");
        return false;
    }
    next.port = port;
    next.qos = qos;
    brokerConfigData = next;
    brokerRev++;
    saveRequested = true;
    return true;
}

bool config::setCalibration(float tempOffset, float humOffset, String &error)
{
    if (isnan(tempOffset) || isnan(humOffset) || fabsf(tempOffset) > TEMP_OFFSET_MAX || fabsf(humOffset) > HUM_OFFSET_MAX)
    {
        error = F("Offsets must be within +/-10 C and +/-20 %RH");
        return false;
    }
    sensorConfigData.tempOffset = tempOffset;
    sensorConfigData.humOffset = humOffset;
    saveRequested = true;
    return true;
}

static bool validTZ(const char *tz)
{
    // POSIX TZ: printable ASCII, no spaces. Full syntax is checked by the C library.
    for (const char *p = tz; *p; p++)
        if (*p <= ' ' || *p > '~')
            return false;
    return true;
}

bool config::setTimeConfig(const char *ntpServer, const char *tz, String &error)
{
    timeData next = timeConfig;
    if (!copyField(next.ntpServer, sizeof(next.ntpServer), ntpServer ? ntpServer : ""))
    {
        error = F("NTP server must be at most 64 characters");
        return false;
    }
    if (!copyField(next.tz, sizeof(next.tz), tz ? tz : "") || !validTZ(next.tz))
    {
        error = F("Time zone must be a POSIX TZ string of at most 48 characters, without spaces");
        return false;
    }
    timeConfig = next;
    timeRev++;
    saveRequested = true;
    return true;
}

void config::setAutoTimezone(const char *tz)
{
    if (timeConfig.tz[0] != '\0' || !copyField(timeConfig.tz, sizeof(timeConfig.tz), tz))
        return;
    timeRev++;
    saveRequested = true;
}

bool config::setAdminPassword(const char *current, const char *password, String &error)
{
    if (current == nullptr || strcmp(current, admin.password) != 0)
    {
        error = F("Current password is wrong");
        return false;
    }
    if (password == nullptr || strlen(password) < ADMIN_PASS_MIN || strlen(password) >= sizeof(admin.password))
    {
        error = F("New password must be 8 to 64 characters");
        return false;
    }
    copyField(admin.password, sizeof(admin.password), password);
    saveRequested = true;
    return true;
}

void config::setStationStaticIP(uint32_t ip, uint32_t gw, uint32_t sub, uint32_t dns)
{
    internal.ip = ip;
    internal.gw = gw;
    internal.sub = sub;
    internal.dns = dns;
    saveRequested = true;
}

String config::portalSSID() const
{
    if (deviceConfigData.deviceSSID[0] != '\0')
        return String(deviceConfigData.deviceSSID);
    char buf[16];
    snprintf_P(buf, sizeof(buf), PSTR("WoT-%06X"), ESP.getChipId());
    return String(buf);
}

bool config::adminPasswordIsDefault() const
{
    return strcmp(admin.password, adminDefaults.password) == 0;
}

// ---------------------------------------------------------------------------
// One-time import of settings written by firmware v1.0 (raw EEPROM layout).

namespace legacy
{
    struct internalData { uint32_t ip, gw, sub, dns; };
    struct deviceData { char name[32], ssid[32], pass[32], ip[16], gw[16], sub[16]; };
    struct sensorData { uint8_t model; uint8_t gpio; uint32_t sampleRate; };
    struct brokerData { bool publishRequest; char address[16]; uint16_t port; char topic[32]; uint8_t qos; };

    const int OFF_INTERNAL = 0;
    const int OFF_DEVICE = 18 + 1;
    const int OFF_SENSOR = OFF_DEVICE + 195 + 1;
    const int OFF_BROKER = OFF_SENSOR + 8 + 1;

    uint8_t checksum(const void *p, size_t len)
    {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        uint8_t v = 0;
        for (size_t i = 0; i < len; i++)
            v += b[i];
        return (uint8_t)(256 - v);
    }

    template <typename T>
    bool read(int offset, int checksumOffset, T &out)
    {
        EEPROM.get(offset, out);
        uint8_t sum = EEPROM.read(checksumOffset);
        return sum == checksum(&out, sizeof(T));
    }

    // Copies a fixed-size legacy field, forcing NUL termination.
    void copy(char *dst, size_t dstSize, const char *src, size_t srcSize)
    {
        size_t n = strnlen(src, srcSize);
        if (n >= dstSize)
            n = dstSize - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
}

bool config::importLegacyEEPROM()
{
    using namespace legacy;
    EEPROM.begin(512);

    legacy::internalData in;
    legacy::deviceData dev;
    legacy::sensorData sen;
    legacy::brokerData bro;

    bool okIn = read(OFF_INTERNAL, 18, in);
    bool okDev = read(OFF_DEVICE, OFF_DEVICE + 195, dev);
    bool okSen = read(OFF_SENSOR, OFF_SENSOR + 8, sen);
    bool okBro = read(OFF_BROKER, OFF_BROKER + 60, bro);
    EEPROM.end();

    // An erased EEPROM (all 0xFF) can pass a checksum by accident, so also
    // require plausible values.
    okDev = okDev && dev.name[0] != (char)0xFF && strnlen(dev.pass, 32) >= AP_PASS_MIN;
    okSen = okSen && sen.sampleRate != 0xFFFFFFFF;
    okBro = okBro && bro.port != 0xFFFF;
    okIn = okIn && in.ip != 0xFFFFFFFF;

    // v1.0 always wrote all blocks together, so require all of them.
    if (!(okDev && okSen && okBro))
        return false;

    copy(deviceConfigData.deviceName, sizeof(deviceConfigData.deviceName), dev.name, sizeof(dev.name));
    copy(deviceConfigData.deviceSSID, sizeof(deviceConfigData.deviceSSID), dev.ssid, sizeof(dev.ssid));
    copy(deviceConfigData.devicePass, sizeof(deviceConfigData.devicePass), dev.pass, sizeof(dev.pass));
    copy(deviceConfigData.deviceIP, sizeof(deviceConfigData.deviceIP), dev.ip, sizeof(dev.ip));
    copy(deviceConfigData.deviceGw, sizeof(deviceConfigData.deviceGw), dev.gw, sizeof(dev.gw));
    copy(deviceConfigData.deviceSub, sizeof(deviceConfigData.deviceSub), dev.sub, sizeof(dev.sub));
    if (!isValidIP(deviceConfigData.deviceIP) || !isValidIP(deviceConfigData.deviceGw) || !isValidIP(deviceConfigData.deviceSub))
    {
        memcpy(deviceConfigData.deviceIP, deviceDefaults.deviceIP, sizeof(deviceConfigData.deviceIP));
        memcpy(deviceConfigData.deviceGw, deviceDefaults.deviceGw, sizeof(deviceConfigData.deviceGw));
        memcpy(deviceConfigData.deviceSub, deviceDefaults.deviceSub, sizeof(deviceConfigData.deviceSub));
    }
    sensorConfigData.sampleRate = constrain(sen.sampleRate, SAMPLE_RATE_MIN_MS, SAMPLE_RATE_MAX_MS);
    brokerConfigData.publishRequest = bro.publishRequest;
    copy(brokerConfigData.address, sizeof(brokerConfigData.address), bro.address, sizeof(bro.address));
    brokerConfigData.port = bro.port;
    copy(brokerConfigData.topic, sizeof(brokerConfigData.topic), bro.topic, sizeof(bro.topic));
    brokerConfigData.qos = min<uint8_t>(bro.qos, 2);
    if (okIn)
        internal = {in.ip, in.gw, in.sub, in.dns};
    return true;
}

config configManager;
