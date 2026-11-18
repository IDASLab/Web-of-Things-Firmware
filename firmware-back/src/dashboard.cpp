#include <ArduinoJson.h>

#include "dashboard.h"
#include "webServer.h"

void dashboard::begin()
{
    server.ws.onEvent(onWsEvent);
}

void dashboard::loop()
{
    // Free memory held by WebSocket clients that went away.
    if (millis() - lastCleanup > 1000)
    {
        lastCleanup = millis();
        server.ws.cleanupClients();
    }
}

void dashboard::send()
{
    if (server.ws.count() == 0)
        return;

    JsonDocument root;
    root["timestamp"] = millis();
    root["sensorstatus"] = data.sensorStatus;
    root["networkstatus"] = data.networkStatus;
    root["brokerstatus"] = data.brokerStatus;
    if (!isnan(data.temperature))
    {
        root["temperature"] = serialized(String(data.temperature, 1));
        root["humidity"] = serialized(String(data.humidity, 1));
    }

    char payload[160];
    serializeJson(root, payload, sizeof(payload));
    server.ws.textAll(payload);
}

void dashboard::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *dataIn, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.println(F("New WS client connected"));
        dash.send(); // show values immediately instead of after the next sample
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.println(F("WS client disconnected"));
    }
}

dashboard dash;
