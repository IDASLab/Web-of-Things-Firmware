#ifndef SERVER_H
#define SERVER_H

#include <ESPAsyncWebServer.h>

class webServer
{
private:
    AsyncAuthenticationMiddleware basicAuth;
    AsyncMiddlewareFunction sameOrigin = AsyncMiddlewareFunction(checkSameOrigin);

    static void checkSameOrigin(AsyncWebServerRequest *request, ArMiddlewareNext next);
    static void handleFileUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final);
    static void serveProgmem(AsyncWebServerRequest *request);
    void bindAll();

public:
    AsyncWebServer server = AsyncWebServer(80);
    AsyncWebSocket ws = AsyncWebSocket("/ws");
    void begin();
    // Call after the admin password changes.
    void updateCredentials();
};

extern webServer server;

#endif
