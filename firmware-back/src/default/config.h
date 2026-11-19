#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Firmware identity. Compiled in (not stored in flash) so it is always
// correct after an update.
#define FIRMWARE_NAME "Helenor"
#define FIRMWARE_VERSION "v1.3.1"

// Field sizes include the terminating NUL.
#define LEN_NAME 33
#define LEN_SSID 33      // 802.11 SSID max 32 chars
#define LEN_WIFI_PASS 65 // WPA2 passphrase max 63 chars (64 = hex PSK)
#define LEN_IP 16
#define LEN_HOST 65
#define LEN_TOPIC 65
#define LEN_ADMIN_PASS 65

// Limits enforced on input
#define AP_PASS_MIN 8
#define AP_PASS_MAX 63
#define ADMIN_PASS_MIN 8
#define SAMPLE_RATE_MIN_MS 2000UL     // DHT11/DHT22 cannot be read faster than every ~2 s
#define SAMPLE_RATE_MAX_MS 3600000UL  // 1 hour

struct deviceData
{
    char deviceName[LEN_NAME];
    char deviceSSID[LEN_SSID];      // captive portal (access point) SSID; empty = "WoT-<chip id>"
    char devicePass[LEN_WIFI_PASS]; // captive portal password (8..63 chars)
    char deviceIP[LEN_IP];          // captive portal address
    char deviceGw[LEN_IP];
    char deviceSub[LEN_IP];
};

// Static IP settings for the station (client) connection; all zero = DHCP
struct networkData
{
    uint32_t ip;
    uint32_t gw;
    uint32_t sub;
    uint32_t dns;
};

struct sensorData
{
    uint8_t model;       // 11 = DHT11, 21 = DHT21, 22 = DHT22
    uint8_t gpio;
    uint32_t sampleRate; // milliseconds
    float tempOffset;    // calibration, added to the raw reading (degrees C)
    float humOffset;     // calibration, added to the raw reading (% RH)
};

#define LEN_TZ 49
#define TEMP_OFFSET_MAX 10.0f
#define HUM_OFFSET_MAX 20.0f

struct timeData
{
    char ntpServer[LEN_HOST]; // empty = NTP off
    char tz[LEN_TZ];          // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"; empty = set from the browser
};

struct brokerData
{
    bool publishRequest;
    char address[LEN_HOST]; // IP address or hostname
    uint16_t port;
    char topic[LEN_TOPIC];
    uint8_t qos;
};

struct adminData
{
    char password[LEN_ADMIN_PASS];
};

extern const deviceData deviceDefaults;
extern const sensorData sensorDefaults;
extern const brokerData brokerDefaults;
extern const adminData adminDefaults;
extern const timeData timeDefaults;

#define ADMIN_USER "admin"

#endif
