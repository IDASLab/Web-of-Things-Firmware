#ifndef DHTSENSOR_H
#define DHTSENSOR_H

#include <Arduino.h>
#include <DHT.h>

class DHTSensor
{
public:
    // Pin and model come from the saved configuration.
    void begin(uint8_t pin, uint8_t model);
    void loop();

    // True once per new sample; the caller then reads the values below.
    bool hasNewSample();

    bool ok() const { return lastReadOk; }
    float temperature() const { return lastTemperature; }
    float humidity() const { return lastHumidity; }
    uint32_t errorCount() const { return consecutiveErrors; }
    // Milliseconds since the last valid reading (UINT32_MAX if none yet).
    uint32_t ageMs() const;
    String sensorType() const;

private:
    DHT *dht = nullptr;
    uint8_t model = 0;
    uint32_t nextSample = 0;
    bool started = false;
    bool newSample = false;

    bool lastReadOk = false;
    float lastTemperature = NAN;
    float lastHumidity = NAN;
    uint32_t lastGoodMillis = 0;
    bool everGood = false;
    uint32_t consecutiveErrors = 0;

    bool retryPending = false;
    uint32_t retryAt = 0;

    bool readOnce(float &t, float &h);
    void finishSample(bool ok, float t, float h, uint32_t now);
};

extern DHTSensor dhtsensor;

#endif
