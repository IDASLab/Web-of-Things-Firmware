#include <Arduino.h>
#include <LittleFS.h>

#include "WiFiManager.h"
#include "webServer.h"
#include "updater.h"
#include "configManager.h"
#include "publisher.h"
#include "dashboard.h"
#include "sensor.h"
#include "system.h"

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.printf_P(PSTR(">> %s %s starting\n"), FIRMWARE_NAME, FIRMWARE_VERSION);

    // File system first: settings live there.
    if (!LittleFS.begin())
    {
        Serial.println(F("LittleFS mount failed, formatting..."));
        LittleFS.format();
        LittleFS.begin();
    }

    configManager.begin();
    WiFiManager.begin();
    server.begin();
    dash.begin();
    mqttPublisher.begin();
    dhtsensor.begin(configManager.sensorConfigData.gpio, configManager.sensorConfigData.model);
}

void loop()
{
    configManager.loop();
    WiFiManager.loop();
    updater.loop();
    dhtsensor.loop();

    // One sampler drives everything, so dashboard and MQTT always show the
    // reading that was just taken.
    if (dhtsensor.hasNewSample())
    {
        dash.data.sensorStatus = dhtsensor.ok();
        dash.data.temperature = dhtsensor.temperature();
        dash.data.humidity = dhtsensor.humidity();
        dash.send();
        if (dhtsensor.ok())
            mqttPublisher.publish(dhtsensor.temperature(), dhtsensor.humidity());
    }

    mqttPublisher.loop();
    dash.loop();
    systemControl.loop();
}
