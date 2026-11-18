#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>

class Publisher
{
public:
    void begin();
    void loop();
    // Publish one reading (call after each valid sensor sample).
    void publish(float temperature, float humidity);
    String getBrokerAddress();
    const char *getBrokerTopic();
    bool connected() { return mqttClient.connected(); }

private:
    AsyncMqttClient mqttClient;
    char host[65] = "";
    char topic[65] = "";
    char clientId[24] = "";
    uint16_t port = 0;
    uint8_t qos = 0;
    uint32_t appliedRevision = UINT32_MAX;
    uint32_t nextConnectAttempt = 0;
    uint32_t retryDelay = 2000;

    void applyConfig();
};

extern Publisher mqttPublisher;

#endif
