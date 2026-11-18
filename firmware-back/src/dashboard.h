#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <ESPAsyncWebServer.h>

struct dashboardData
{
	bool sensorStatus = false;
	bool networkStatus = false;
	bool brokerStatus = false;
	float temperature = NAN; // degrees C
	float humidity = NAN;    // % RH
};

class dashboard
{
private:
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    uint32_t lastCleanup = 0;

public:
    void begin();
    void loop();
    // Push the current values to all WebSocket clients.
    void send();
    dashboardData data;
};

extern dashboard dash;

#endif
