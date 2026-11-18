#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <LittleFS.h>

#include "webServer.h"
#include "default/html.h"

#include "WiFiManager.h"
#include "configManager.h"
#include "updater.h"
#include "publisher.h"
#include "sensor.h"
#include "dashboard.h"
#include "system.h"

// Define WOT_ENABLE_CORS (build flag) only while developing the front end
// on another origin. It weakens the protection against malicious web pages.

static const char JSON_TYPE[] = "application/json";

static void sendOk(AsyncWebServerRequest *request, const char *message = "message is received")
{
    JsonDocument doc;
    doc["status"] = "successed"; // spelling kept for the existing front end
    doc["message"] = message;
    String out;
    serializeJson(doc, out);
    request->send(200, JSON_TYPE, out);
}

static void sendError(AsyncWebServerRequest *request, int code, const String &message)
{
    JsonDocument doc;
    doc["status"] = "failed";
    doc["message"] = message;
    String out;
    serializeJson(doc, out);
    request->send(code, JSON_TYPE, out);
}

// Accept only plain file names at the root of the file system:
// letters, digits, '.', '_', '-', at most 31 characters, not hidden.
static bool sanitizeFilename(String name, String &path)
{
    int slash = name.lastIndexOf('/');
    if (slash >= 0)
        name = name.substring(slash + 1);
    if (name.length() == 0 || name.length() > 31 || name[0] == '.')
        return false;
    for (size_t i = 0; i < name.length(); i++)
    {
        char c = name[i];
        if (!(isalnum(c) || c == '.' || c == '_' || c == '-'))
            return false;
    }
    path = "/" + name;
    return true;
}

static const char *str(JsonVariantConst v, const char *fallback = "")
{
    const char *s = v.as<const char *>();
    return s ? s : fallback;
}

// Registers a POST endpoint with a JSON body. The library assembles chunked
// bodies and rejects requests without "Content-Type: application/json"
// (which a cross-site form cannot send).
static void onJson(AsyncWebServer &srv, const char *uri, std::function<void(AsyncWebServerRequest *, JsonObjectConst)> fn)
{
    AsyncCallbackJsonWebHandler *h = new AsyncCallbackJsonWebHandler(uri, [fn](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>())
        {
            sendError(request, 400, F("JSON object expected"));
            return;
        }
        fn(request, json.as<JsonObjectConst>());
    });
    h->setMethod(HTTP_POST);
    h->setMaxContentLength(1024);
    srv.addHandler(h);
}

void webServer::checkSameOrigin(AsyncWebServerRequest *request, ArMiddlewareNext next)
{
#ifndef WOT_ENABLE_CORS
    // Block cross-site requests that change state (CSRF): a page on another
    // site could otherwise reset or reflash the device through your browser.
    if (request->method() != HTTP_GET && request->method() != HTTP_HEAD && request->method() != HTTP_OPTIONS)
    {
        String source = request->header("Origin");
        if (source.length() == 0)
            source = request->header("Referer");
        if (source.length() > 0)
        {
            int start = source.indexOf("://");
            String hostPart = start >= 0 ? source.substring(start + 3) : source;
            int end = hostPart.indexOf('/');
            if (end >= 0)
                hostPart = hostPart.substring(0, end);
            if (!hostPart.equalsIgnoreCase(request->host()))
            {
                request->send(403, "text/plain", "Cross-origin request blocked");
                return;
            }
        }
    }
#endif
    next();
}

void webServer::updateCredentials()
{
    basicAuth.setUsername(ADMIN_USER);
    basicAuth.setPassword(configManager.admin.password);
    basicAuth.generateHash();
}

void webServer::begin()
{
#ifdef WOT_ENABLE_CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
#endif

    // Everything (page, API, WebSocket, downloads) needs the admin login.
    basicAuth.setRealm("WoT Device");
    basicAuth.setAuthFailureMessage("Authentication required");
    basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    updateCredentials();
    server.addMiddlewares({&sameOrigin, &basicAuth});

    server.addHandler(&ws);
    bindAll();
    server.onNotFound(serveProgmem);
    server.begin();

    if (configManager.adminPasswordIsDefault())
        Serial.println(F("WARNING: web login is still admin/admin. Change it with POST /api/config/admin/set"));
}

void webServer::bindAll()
{
    server.on("/api/index", HTTP_OPTIONS | HTTP_GET, [](AsyncWebServerRequest *request) {
        sendOk(request, "WoT Device is alive!");
    });

    // ---- Network ---------------------------------------------------------

    onJson(server, "/api/network/setStaticIP", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        const char *ssid = str(doc["ssid"]);
        if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(str(doc["pass"])) > 64)
            return sendError(request, 400, F("Invalid SSID or password"));
        if (!isValidIP(str(doc["ip"])) || !isValidIP(str(doc["subnet"])) || !isValidIP(str(doc["gateway"])) || !isValidIP(str(doc["dns"])))
            return sendError(request, 400, F("Invalid IP, subnet, gateway or DNS address"));
        sendOk(request);
        WiFiManager.setNewStaticWifi(ssid, str(doc["pass"]), str(doc["ip"]), str(doc["subnet"]), str(doc["gateway"]), str(doc["dns"]));
    });

    onJson(server, "/api/network/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        const char *ssid = str(doc["ssid"]);
        if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(str(doc["pass"])) > 64)
            return sendError(request, 400, F("Invalid SSID or password"));
        sendOk(request);
        WiFiManager.setNewWifi(ssid, str(doc["pass"]));
    });

    server.on("/api/network/forget", HTTP_POST, [](AsyncWebServerRequest *request) {
        sendOk(request, "Network information is cleared successfully");
        WiFiManager.forget();
    });

    server.on("/api/network/get", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["status"] = WiFiManager.isCaptivePortal();
        doc["ssid"] = configManager.portalSSID();
        doc["pass"] = ""; // never sent back
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    // ---- Configuration ---------------------------------------------------

    onJson(server, "/api/config/device/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!configManager.setDeviceConfig(str(doc["name"]), str(doc["ssid"]), str(doc["pass"]), error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    onJson(server, "/api/config/device/setStatic", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!configManager.setStaticDeviceConfig(str(doc["name"]), str(doc["ssid"]), str(doc["pass"]),
                                                 str(doc["ip"]), str(doc["subnet"]), str(doc["gateway"]), error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    onJson(server, "/api/config/sensor/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!doc["rate"].is<uint32_t>())
            return sendError(request, 400, F("rate (milliseconds) is required"));
        if (!configManager.setSensorConfig(doc["rate"].as<uint32_t>(), error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    onJson(server, "/api/config/broker/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        uint32_t port = doc["port"] | 0u;
        uint32_t qos = doc["qos"] | 0u;
        if (port > 65535 || qos > 2)
            return sendError(request, 400, F("Invalid port or QoS"));
        if (!configManager.setBrokerConfig(doc["status"] | false, str(doc["ip"]), port, str(doc["topic"]), qos, error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    onJson(server, "/api/config/admin/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!configManager.setAdminPassword(str(doc["current"], nullptr), str(doc["password"], nullptr), error))
            return sendError(request, 400, error);
        sendOk(request, "Admin password changed");
        ::server.updateCredentials();
    });

    server.on("/api/config/get", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["DeviceName"] = configManager.deviceConfigData.deviceName;
        doc["DeviceSSID"] = configManager.portalSSID();
        doc["DevicePassword"] = ""; // never sent back; leave empty to keep it
        doc["DeviceIP"] = configManager.deviceConfigData.deviceIP;
        doc["DeviceGw"] = configManager.deviceConfigData.deviceGw;
        doc["DeviceSub"] = configManager.deviceConfigData.deviceSub;

        doc["MQTTStatus"] = configManager.brokerConfigData.publishRequest;
        doc["MQTTBrokerIP"] = configManager.brokerConfigData.address;
        doc["MQTTBrokerPort"] = configManager.brokerConfigData.port;
        doc["MQTTBrokerTopic"] = configManager.brokerConfigData.topic;
        doc["MQTTBrokerQoS"] = configManager.brokerConfigData.qos;

        doc["SensorModel"] = configManager.sensorConfigData.model;
        doc["SensorGPIO"] = configManager.sensorConfigData.gpio;
        doc["SampelingRate"] = configManager.sensorConfigData.sampleRate; // key spelling kept for the front end

        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    // ---- Device ----------------------------------------------------------

    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        sendOk(request, "Device is restarted!!");
        systemControl.requestRestart();
    });

    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        sendOk(request, "Device is reset to factory defaults!!");
        systemControl.requestFactoryReset();
    });

    server.on("/api/dashboard/get", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["firmwareName"] = FIRMWARE_NAME;
        doc["firmwareVer"] = FIRMWARE_VERSION;
        doc["sensorModel"] = configManager.sensorConfigData.model;
        doc["deviceName"] = configManager.deviceConfigData.deviceName;
        doc["networkSSID"] = WiFiManager.SSID();
        doc["networkAddress"] = WiFiManager.getAddress();
        doc["brokerServer"] = mqttPublisher.getBrokerAddress();
        doc["brokerTopic"] = mqttPublisher.getBrokerTopic();
        doc["sensorErrors"] = dhtsensor.errorCount();
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    // ---- Files and firmware update ---------------------------------------

    server.on("/api/files/get", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray files = doc["files"].to<JsonArray>();
        Dir dir = LittleFS.openDir("/");
        while (dir.next())
        {
            if (dir.isFile()) // skips the /sys settings folder
                files.add(dir.fileName());
        }
        FSInfo fs_info;
        LittleFS.info(fs_info);
        doc["used"] = String(fs_info.usedBytes);
        doc["max"] = String(fs_info.totalBytes);
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path;
        String url = request->url();
        if (!url.startsWith("/download/") || !sanitizeFilename(url.substring(10), path) || !LittleFS.exists(path))
            return request->send(404, "text/plain", "File not found");
        request->send(LittleFS, path, "application/octet-stream", true);
    });

    server.on("/api/files/remove", HTTP_POST, [](AsyncWebServerRequest *request) {
        String path;
        if (!sanitizeFilename(request->arg("filename"), path) || !LittleFS.exists(path))
            return sendError(request, 404, F("File not found"));
        LittleFS.remove(path);
        request->send(200, "text/html", "");
    });

    server.on("/api/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        String path;
        if (!sanitizeFilename(request->arg("filename"), path) || !LittleFS.exists(path))
            return sendError(request, 404, F("File not found"));
        updater.requestStart(path);
        request->send(200, "text/html", "");
    });

    server.on("/api/update-status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["status"] = updater.getStatus();
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Runs after the whole body was received.
        bool ok = request->_tempObject != nullptr && *static_cast<bool *>(request->_tempObject);
        request->send(200, JSON_TYPE, ok ? "{\"success\":true}" : "{\"success\":false}");
    }, handleFileUpload);
}

void webServer::serveProgmem(AsyncWebServerRequest *request)
{
    // Gzipped single-page UI stored in flash.
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", html, html_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void webServer::handleFileUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
    // Per-request state (the old code shared one static File between uploads).
    // _tempObject is freed by the request; _tempFile is closed with it.
    if (index == 0)
    {
        bool *ok = static_cast<bool *>(malloc(sizeof(bool)));
        if (ok == nullptr)
            return;
        *ok = false;
        request->_tempObject = ok;

        String path;
        if (!sanitizeFilename(filename, path))
        {
            Serial.println(F("Upload rejected: invalid file name"));
            return;
        }
        FSInfo info;
        LittleFS.info(info);
        size_t freeBytes = info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
        // Keep a margin for file system metadata and the settings file.
        if (request->contentLength() + 16384 > freeBytes)
        {
            Serial.println(F("Upload rejected: not enough space"));
            return;
        }
        request->_tempFile = LittleFS.open(path, "w");
        if (!request->_tempFile)
        {
            Serial.println(F("Upload failed: cannot create file"));
            return;
        }
        *ok = true;
        Serial.printf_P(PSTR("Upload started: %s\n"), path.c_str());
    }

    bool *ok = static_cast<bool *>(request->_tempObject);
    if (ok == nullptr || !*ok)
        return;

    if (len > 0 && request->_tempFile.write(data, len) != len)
    {
        Serial.println(F("Upload failed: write error (flash full?)"));
        *ok = false;
    }

    if (final)
    {
        String path = request->_tempFile.fullName();
        request->_tempFile.close();
        if (!path.startsWith("/"))
            path = "/" + path;
        if (!*ok)
            LittleFS.remove(path);
        Serial.printf_P(PSTR("Upload %s\n"), *ok ? "complete" : "failed");
    }
}

webServer server;
