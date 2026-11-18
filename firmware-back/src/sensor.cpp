#include "sensor.h"
#include "configManager.h"

static uint8_t dhtType(uint8_t model)
{
    switch (model)
    {
    case 21: return DHT21;
    case 22: return DHT22;
    default: return DHT11;
    }
}

void DHTSensor::begin(uint8_t pin, uint8_t sensorModel)
{
    model = (sensorModel == 21 || sensorModel == 22) ? sensorModel : 11;
    dht = new DHT(pin, dhtType(model));
    dht->begin();
    Serial.printf_P(PSTR("Sensor %s on GPIO %u, sampling every %lu ms\n"),
                    sensorType().c_str(), pin, (unsigned long)configManager.sensorConfigData.sampleRate);
    // The DHT needs ~1-2 s after power-up before the first valid read.
    nextSample = millis() + 2000;
    started = true;
}

bool DHTSensor::readOnce(float &t, float &h)
{
    // force=true: we already control the timing ourselves.
    h = dht->readHumidity(true);
    t = dht->readTemperature(false, false); // reuses the frame read above
    return !isnan(t) && !isnan(h);
}

void DHTSensor::loop()
{
    if (!started)
        return;

    uint32_t now = millis();

    if (retryPending)
    {
        if ((int32_t)(now - retryAt) < 0)
            return;
        retryPending = false;
        float t, h;
        finishSample(readOnce(t, h), t, h, now);
        return;
    }

    if ((int32_t)(now - nextSample) < 0)
        return;

    // Fixed cadence that does not drift. If we fell far behind (for example
    // after a long blocking call), skip ahead instead of bursting.
    uint32_t interval = configManager.sensorConfigData.sampleRate;
    nextSample += interval;
    if ((int32_t)(now - nextSample) >= 0)
        nextSample = now + interval;

    float t, h;
    if (readOnce(t, h))
    {
        finishSample(true, t, h, now);
    }
    else
    {
        // Single bad frames are common (long wires, noise). Retry once after
        // the minimum time the sensor needs between reads, without blocking.
        retryPending = true;
        retryAt = now + 1200;
    }
}

void DHTSensor::finishSample(bool ok, float t, float h, uint32_t now)
{
    if (ok)
    {
        // Calibration offsets from the settings
        t += configManager.sensorConfigData.tempOffset;
        h = constrain(h + configManager.sensorConfigData.humOffset, 0.0f, 100.0f);
        lastTemperature = t;
        lastHumidity = h;
        lastGoodMillis = now;
        everGood = true;
        consecutiveErrors = 0;
        Serial.printf_P(PSTR("Temperature: %.1f C  Humidity: %.1f %%\n"), t, h);
    }
    else
    {
        consecutiveErrors++;
        Serial.printf_P(PSTR("Error reading sensor (%lu in a row)\n"), (unsigned long)consecutiveErrors);
    }
    lastReadOk = ok;
    newSample = true;
}

bool DHTSensor::hasNewSample()
{
    bool n = newSample;
    newSample = false;
    return n;
}

uint32_t DHTSensor::ageMs() const
{
    return everGood ? millis() - lastGoodMillis : UINT32_MAX;
}

String DHTSensor::sensorType() const
{
    return String(F("DHT")) + String(model);
}

DHTSensor dhtsensor;
