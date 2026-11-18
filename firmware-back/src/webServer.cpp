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
#include "logger.h"
#include "timeSync.h"
#include <memory>

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


// ---- Logger helpers -------------------------------------------------------

static void sessionToJson(JsonObject o, const SessionInfo &s)
{
    o["id"] = s.id;
    o["name"] = s.name;
    o["notes"] = s.notes;
    o["active"] = s.active;
    o["startEpoch"] = s.startEpoch;
    o["endEpoch"] = s.endEpoch;
    o["intervalMs"] = s.intervalMs;
    o["tempOffset"] = s.tempOffset;
    o["humOffset"] = s.humOffset;
    o["resumes"] = s.resumes;
    o["errors"] = s.errors;
    o["stopReason"] = s.stopReason;
    o["count"] = logger.recordCount(s.id);
    if (s.temp.n)
    {
        JsonObject t = o["temp"].to<JsonObject>();
        t["min"] = serialized(String(s.temp.min, 1));
        t["max"] = serialized(String(s.temp.max, 1));
        t["avg"] = serialized(String(s.temp.sum / s.temp.n, 2));
        JsonObject h = o["hum"].to<JsonObject>();
        h["min"] = serialized(String(s.hum.min, 1));
        h["max"] = serialized(String(s.hum.max, 1));
        h["avg"] = serialized(String(s.hum.sum / s.hum.n, 2));
    }
}

static void timeToJson(JsonObject o)
{
    o["valid"] = timeSync.valid();
    o["epoch"] = (uint32_t)timeSync.now();
    if (timeSync.valid())
        o["local"] = TimeSync::formatLocal(timeSync.now());
    o["source"] = timeSync.sourceName();
    o["tz"] = configManager.timeConfig.tz;
    o["ntp"] = configManager.timeConfig.ntpServer;
}

static void fmt10(char *dst, size_t n, int v)
{
    // v is a value x10, printed with one decimal
    snprintf_P(dst, n, PSTR("%s%d.%d"), v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10);
}

// Streams rows of a session without building the whole response in RAM.
struct RowStream
{
    uint32_t id, next, end, step;
    bool headerDone = false, footerDone = false;
    String pending;
    std::function<String(const LogRecord &)> row;
    String header, footer;
};

static AsyncWebServerResponse *streamRows(AsyncWebServerRequest *request, const char *type, std::shared_ptr<RowStream> st)
{
    return request->beginChunkedResponse(type, [st](uint8_t *buf, size_t maxLen, size_t) -> size_t {
        size_t used = 0;
        auto put = [&](const String &text) -> bool {
            if (text.length() > maxLen - used)
                return false;
            memcpy(buf + used, text.c_str(), text.length());
            used += text.length();
            return true;
        };
        if (st->pending.length())
        {
            if (!put(st->pending))
            {
                // Larger than a whole chunk: should not happen with short rows.
                st->pending = "";
                return 0;
            }
            st->pending = "";
        }
        if (!st->headerDone)
        {
            if (!put(st->header))
                return used;
            st->headerDone = true;
        }
        while (st->next < st->end && used < maxLen)
        {
            LogRecord r;
            uint32_t idx = st->next;
            st->next += st->step;
            if (!logger.readRecord(st->id, idx, r))
            {
                st->next = st->end;
                break;
            }
            String line = st->row(r);
            if (!put(line))
            {
                st->pending = line;
                return used;
            }
        }
        if (st->next >= st->end && !st->footerDone)
        {
            if (!put(st->footer))
                return used;
            st->footerDone = true;
        }
        return used;
    });
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
    server.onNotFound([](AsyncWebServerRequest *request) {
        // Unknown API calls get a JSON 404; any other path opens the web app.
        if (request->url().startsWith("/api/"))
            return sendError(request, 404, F("Unknown API endpoint"));
        serveProgmem(request);
    });
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


    // ---- Time and calibration --------------------------------------------

    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        timeToJson(doc.to<JsonObject>());
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    onJson(server, "/api/time/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        if (!doc["epoch"].is<uint32_t>())
            return sendError(request, 400, F("epoch (unix seconds) is required"));
        if (!timeSync.setFromBrowser(doc["epoch"].as<uint32_t>(), doc["offset"] | 0))
            return sendError(request, 400, F("Invalid time"));
        sendOk(request, "Time set");
    });

    onJson(server, "/api/config/time/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!configManager.setTimeConfig(str(doc["ntp"]), str(doc["tz"]), error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    onJson(server, "/api/config/calibration/set", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (!doc["temp"].is<float>() || !doc["hum"].is<float>())
            return sendError(request, 400, F("temp and hum offsets are required"));
        if (!configManager.setCalibration(doc["temp"].as<float>(), doc["hum"].as<float>(), error))
            return sendError(request, 400, error);
        sendOk(request);
    });

    // ---- Experiment sessions ---------------------------------------------

    server.on("/api/logger/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["firmwareVer"] = FIRMWARE_VERSION;
        doc["deviceName"] = configManager.deviceConfigData.deviceName;
        doc["sensor"] = dhtsensor.sensorType();
        doc["sampleRate"] = configManager.sensorConfigData.sampleRate;
        doc["tempOffset"] = configManager.sensorConfigData.tempOffset;
        doc["humOffset"] = configManager.sensorConfigData.humOffset;
        JsonObject now = doc["reading"].to<JsonObject>();
        now["ok"] = dhtsensor.ok();
        if (!isnan(dhtsensor.temperature()))
        {
            now["temperature"] = serialized(String(dhtsensor.temperature(), 1));
            now["humidity"] = serialized(String(dhtsensor.humidity(), 1));
            now["ageS"] = dhtsensor.ageMs() / 1000;
        }
        now["errors"] = dhtsensor.errorCount();
        timeToJson(doc["time"].to<JsonObject>());
        JsonObject st = doc["storage"].to<JsonObject>();
        st["free"] = logger.freeBytes();
        st["total"] = logger.totalBytes();
        st["capacityRecords"] = logger.capacityRecords();
        doc["active"] = logger.active();
        if (logger.active())
        {
            sessionToJson(doc["session"].to<JsonObject>(), logger.activeSession());
            doc["session"]["elapsed"] = logger.elapsedNow();
        }
        doc["network"] = WiFiManager.isConnected();
        doc["broker"] = mqttPublisher.connected();
        String out;
        serializeJson(doc, out);
        request->send(200, JSON_TYPE, out);
    });

    onJson(server, "/api/session/start", [](AsyncWebServerRequest *request, JsonObjectConst doc) {
        String error;
        if (doc["rate"].is<uint32_t>() && !configManager.setSensorConfig(doc["rate"].as<uint32_t>(), error))
            return sendError(request, 400, error);
        if (!logger.start(str(doc["name"]), str(doc["notes"]), error))
            return sendError(request, 409, error);
        sendOk(request, "Session started");
    });

    server.on("/api/session/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!logger.active())
            return sendError(request, 409, F("No session is running"));
        logger.stop();
        sendOk(request, "Session stopped");
    });

    server.on("/api/session/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        String error;
        if (!logger.remove(request->arg("id").toInt(), error))
            return sendError(request, 400, error);
        sendOk(request, "Session deleted");
    });

    server.on("/api/sessions", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Streamed as built: the list can get long.
        AsyncResponseStream *response = request->beginResponseStream(JSON_TYPE);
        response->print("{\"sessions\":[");
        bool first = true;
        logger.forEachSession([&](const SessionInfo &s) {
            JsonDocument doc;
            sessionToJson(doc.to<JsonObject>(), s);
            if (!first)
                response->print(',');
            first = false;
            serializeJson(doc, *response);
        });
        response->print("]}");
        request->send(response);
    });

    // Readings as JSON for the chart:
    //   ?id=N&from=0&step=1&max=2000 -> {"id":N,"count":C,"next":I,"rows":[[elapsed,epoch,t,h],...]}
    server.on("/api/session/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint32_t id = request->arg("id").toInt();
        SessionInfo info;
        bool isActive = logger.active() && logger.activeSession().id == id;
        if (!isActive && !logger.loadInfo(id, info))
            return sendError(request, 404, F("Session not found"));
        uint32_t count = logger.recordCount(id);
        uint32_t from = request->hasArg("from") ? request->arg("from").toInt() : 0;
        uint32_t step = max<long>(1, request->hasArg("step") ? request->arg("step").toInt() : 1);
        uint32_t maxRows = constrain(request->hasArg("max") ? request->arg("max").toInt() : 2000, 1, 5000);
        if (from > count)
            from = count;
        uint64_t endWanted = (uint64_t)from + (uint64_t)maxRows * step;
        uint32_t end = endWanted < count ? (uint32_t)endWanted : count;
        uint32_t rows = end > from ? (end - from + step - 1) / step : 0;
        uint32_t next = from + rows * step;

        auto st = std::make_shared<RowStream>();
        st->id = id;
        st->next = from;
        st->end = end;
        st->step = step;
        st->header = String(F("{\"id\":")) + id + F(",\"count\":") + count + F(",\"next\":") + next + F(",\"rows\":[");
        st->footer = "]}";
        std::shared_ptr<bool> firstRow = std::make_shared<bool>(true);
        st->row = [firstRow](const LogRecord &r) {
            char t[12] = "null", h[12] = "null";
            if (r.ok())
            {
                fmt10(t, sizeof(t), r.t10);
                fmt10(h, sizeof(h), r.h10);
            }
            char line[64];
            snprintf_P(line, sizeof(line), PSTR("%s[%u,%u,%s,%s]"), *firstRow ? "" : ",", r.elapsed, r.epoch, t, h);
            *firstRow = false;
            return String(line);
        };
        request->send(streamRows(request, JSON_TYPE, st));
    });

    // CSV export: ?id=N
    server.on("/api/session/csv", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint32_t id = request->arg("id").toInt();
        SessionInfo info;
        bool isActive = logger.active() && logger.activeSession().id == id;
        if (isActive)
            info = logger.activeSession();
        else if (!logger.loadInfo(id, info))
            return sendError(request, 404, F("Session not found"));

        auto st = std::make_shared<RowStream>();
        st->id = id;
        st->next = 0;
        st->end = logger.recordCount(id);
        st->step = 1;
        st->header = F("time_local,unix_time,elapsed_s,temperature_c,humidity_pct,status\r\n");
        uint32_t startEpoch = info.startEpoch;
        st->row = [startEpoch](const LogRecord &r) {
            uint32_t epoch = r.epoch ? r.epoch : (startEpoch ? startEpoch + r.elapsed : 0);
            String local = epoch ? TimeSync::formatLocal(epoch) : String();
            char t[12] = "", h[12] = "";
            if (r.ok())
            {
                fmt10(t, sizeof(t), r.t10);
                fmt10(h, sizeof(h), r.h10);
            }
            char line[112];
            snprintf_P(line, sizeof(line), PSTR("%s,%s,%u,%s,%s,%s\r\n"), local.c_str(),
                       epoch ? String(epoch).c_str() : "", r.elapsed, t, h, r.ok() ? "ok" : "sensor_error");
            return String(line);
        };

        // File name from the session name, safe characters only
        String fname = String(F("session-")) + id + "-";
        for (const char *p = info.name; *p && fname.length() < 60; p++)
            fname += (isalnum(*p) || *p == '-' || *p == '_') ? *p : '_';
        fname += ".csv";

        AsyncWebServerResponse *response = streamRows(request, "text/csv", st);
        response->addHeader("Content-Disposition", String(F("attachment; filename=\"")) + fname + "\"");
        request->send(response);
    });

    // The web app (hash routes inside one page). /logger is kept for old bookmarks.
    server.on("/", HTTP_GET, serveProgmem);
    server.on("/logger", HTTP_GET, serveProgmem);

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
