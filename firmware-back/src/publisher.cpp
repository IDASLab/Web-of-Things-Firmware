#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

#include "publisher.h"
#include "dashboard.h"
#include "configManager.h"

void Publisher::begin()
{
    snprintf_P(clientId, sizeof(clientId), PSTR("wot-%06x"), ESP.getChipId());
    mqttClient.setClientId(clientId);

    // Register callbacks once (they used to be added again on every retry).
    mqttClient.onConnect([this](bool sessionPresent) {
        dash.data.brokerStatus = true;
        retryDelay = 2000;
        Serial.printf_P(PSTR("Connected to MQTT broker %s:%u\n"), host, port);
    });
    mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        dash.data.brokerStatus = false;
        Serial.printf_P(PSTR("MQTT disconnected (reason %d), retrying in %lu s\n"), (int)reason, (unsigned long)(retryDelay / 1000));
        nextConnectAttempt = millis() + retryDelay;
        retryDelay = min<uint32_t>(retryDelay * 2, 60000); // back off up to 1 min
    });
}

void Publisher::applyConfig()
{
    appliedRevision = configManager.brokerRevision();
    if (mqttClient.connected())
        mqttClient.disconnect(true);

    // AsyncMqttClient keeps the pointers, so copy into our own buffers.
    copyField(host, sizeof(host), configManager.brokerConfigData.address);
    copyField(topic, sizeof(topic), configManager.brokerConfigData.topic);
    port = configManager.brokerConfigData.port;
    qos = configManager.brokerConfigData.qos;

    IPAddress ip;
    if (ip.fromString(host))
        mqttClient.setServer(ip, port);
    else
        mqttClient.setServer(host, port); // hostname, resolved by the client

    dash.data.brokerStatus = false;
    retryDelay = 2000;
    nextConnectAttempt = millis();
}

void Publisher::loop()
{
    // Broker settings changed from the web UI: apply without a reboot.
    if (appliedRevision != configManager.brokerRevision())
        applyConfig();

    if (!configManager.brokerConfigData.publishRequest || host[0] == '\0')
    {
        if (mqttClient.connected())
            mqttClient.disconnect();
        return;
    }

    if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED &&
        (int32_t)(millis() - nextConnectAttempt) >= 0)
    {
        // Async: the result arrives in onConnect / onDisconnect.
        nextConnectAttempt = millis() + 30000; // safety net if no callback fires
        mqttClient.connect();
    }
}

void Publisher::publish(float temperature, float humidity)
{
    if (!configManager.brokerConfigData.publishRequest || !mqttClient.connected())
        return;

    JsonDocument doc;
    doc["name"] = configManager.deviceConfigData.deviceName;
    doc["timestamp"] = millis();
    JsonObject value = doc["value"].to<JsonObject>();
    value["temperature"] = serialized(String(temperature, 1));
    value["humidity"] = serialized(String(humidity, 1));

    char payload[192];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(topic, qos, true, payload, len);
}

String Publisher::getBrokerAddress()
{
    return String(host);
}

const char *Publisher::getBrokerTopic()
{
    return topic;
}

Publisher mqttPublisher;
