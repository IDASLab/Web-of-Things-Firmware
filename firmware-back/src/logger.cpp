#include <LittleFS.h>
#include <ArduinoJson.h>

#include "logger.h"
#include "configManager.h"
#include "timeSync.h"
#include "sensor.h"

#define STATE_PATH "/sys/logger.json"

void RunningStats::add(float v)
{
    if (n == 0 || v < min)
        min = v;
    if (n == 0 || v > max)
        max = v;
    sum += v;
    n++;
}

String Logger::binPath(uint32_t id) { return String(F(LOG_DIR "/")) + id + F(".bin"); }
String Logger::infoPath(uint32_t id) { return String(F(LOG_DIR "/")) + id + F(".json"); }

static void statsToJson(JsonObject o, const RunningStats &s)
{
    o["n"] = s.n;
    if (s.n)
    {
        o["min"] = s.min;
        o["max"] = s.max;
        o["sum"] = s.sum;
    }
}

static void statsFromJson(JsonObjectConst o, RunningStats &s)
{
    s.n = o["n"] | 0u;
    if (s.n)
    {
        s.min = o["min"] | NAN;
        s.max = o["max"] | NAN;
        s.sum = o["sum"] | 0.0;
    }
}

bool Logger::saveInfo(const SessionInfo &s)
{
    JsonDocument doc;
    doc["id"] = s.id;
    doc["name"] = s.name;
    doc["notes"] = s.notes;
    doc["startEpoch"] = s.startEpoch;
    doc["endEpoch"] = s.endEpoch;
    doc["intervalMs"] = s.intervalMs;
    doc["tempOffset"] = s.tempOffset;
    doc["humOffset"] = s.humOffset;
    doc["resumes"] = s.resumes;
    doc["errors"] = s.errors;
    doc["stopReason"] = s.stopReason;
    doc["active"] = s.active;
    statsToJson(doc["temp"].to<JsonObject>(), s.temp);
    statsToJson(doc["hum"].to<JsonObject>(), s.hum);

    String tmp = infoPath(s.id) + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f)
        return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    if (!ok)
        return false;
    LittleFS.remove(infoPath(s.id));
    return LittleFS.rename(tmp, infoPath(s.id));
}

bool Logger::loadInfo(uint32_t id, SessionInfo &s)
{
    File f = LittleFS.open(infoPath(id), "r");
    if (!f)
        return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
        return false;
    s = SessionInfo();
    s.id = id;
    copyField(s.name, sizeof(s.name), doc["name"] | "");
    copyField(s.notes, sizeof(s.notes), doc["notes"] | "");
    s.startEpoch = doc["startEpoch"] | 0u;
    s.endEpoch = doc["endEpoch"] | 0u;
    s.intervalMs = doc["intervalMs"] | 0u;
    s.tempOffset = doc["tempOffset"] | 0.0f;
    s.humOffset = doc["humOffset"] | 0.0f;
    s.resumes = doc["resumes"] | 0u;
    s.errors = doc["errors"] | 0u;
    copyField(s.stopReason, sizeof(s.stopReason), doc["stopReason"] | "");
    s.active = doc["active"] | false;
    statsFromJson(doc["temp"], s.temp);
    statsFromJson(doc["hum"], s.hum);
    return true;
}

void Logger::saveState()
{
    LittleFS.mkdir("/sys");
    File f = LittleFS.open(STATE_PATH, "w");
    if (!f)
        return;
    JsonDocument doc;
    doc["nextId"] = nextId;
    doc["activeId"] = current.active ? current.id : 0;
    serializeJson(doc, f);
    f.close();
}

void Logger::begin()
{
    LittleFS.mkdir(LOG_DIR);

    uint32_t activeId = 0;
    File f = LittleFS.open(STATE_PATH, "r");
    if (f)
    {
        JsonDocument doc;
        if (!deserializeJson(doc, f))
        {
            nextId = doc["nextId"] | 1u;
            activeId = doc["activeId"] | 0u;
        }
        f.close();
    }

    if (activeId && loadInfo(activeId, current) && current.active)
    {
        // Continue the session that was running before the reboot.
        current.resumes++;
        fileRecords = recordCount(activeId);
        uint32_t lastElapsed = 0;
        LogRecord last;
        if (fileRecords && readRecord(activeId, fileRecords - 1, last))
            lastElapsed = last.elapsed;
        elapsedAtStartMillis = lastElapsed;
        sessionStartMillis = millis();
        lastFlush = millis();
        saveInfo(current);
        Serial.printf_P(PSTR("Resumed session %u \"%s\" (%u readings)\n"), current.id, current.name, fileRecords);
    }
    else
    {
        current = SessionInfo();
    }
}

uint32_t Logger::elapsedNow() const
{
    if (!current.active)
        return 0;
    // Prefer wall-clock time; fall back to uptime since start/resume.
    if (current.startEpoch && timeSync.valid())
    {
        time_t now = timeSync.now();
        if (now >= (time_t)current.startEpoch)
            return now - current.startEpoch;
    }
    return elapsedAtStartMillis + (millis() - sessionStartMillis) / 1000;
}

bool Logger::start(const char *name, const char *notes, String &error)
{
    if (current.active)
    {
        error = F("A session is already running");
        return false;
    }
    if (freeBytes() < LOG_MIN_FREE_BYTES * 2)
    {
        error = F("Not enough free storage. Delete old sessions first.");
        return false;
    }

    SessionInfo s;
    s.id = nextId;
    if (!copyField(s.name, sizeof(s.name), name ? name : "") || !copyField(s.notes, sizeof(s.notes), notes ? notes : ""))
    {
        error = F("Name max 48 and notes max 200 characters");
        return false;
    }
    if (s.name[0] == '\0')
        snprintf_P(s.name, sizeof(s.name), PSTR("Session %u"), s.id);
    s.startEpoch = timeSync.valid() ? (uint32_t)timeSync.now() : 0;
    s.intervalMs = configManager.sensorConfigData.sampleRate;
    s.tempOffset = configManager.sensorConfigData.tempOffset;
    s.humOffset = configManager.sensorConfigData.humOffset;
    s.active = true;

    generation++;
    LittleFS.remove(binPath(s.id)); // leftover from a deleted state file
    if (!saveInfo(s))
    {
        error = F("Could not write to storage");
        return false;
    }
    current = s;
    nextId++;
    fileRecords = 0;
    buffered = 0;
    elapsedAtStartMillis = 0;
    sessionStartMillis = millis();
    lastFlush = millis();
    saveState();
    Serial.printf_P(PSTR("Session %u \"%s\" started\n"), current.id, current.name);
    return true;
}

void Logger::stop(const char *reason)
{
    if (!current.active)
        return;
    flush();
    current.active = false;
    current.endEpoch = timeSync.valid() ? (uint32_t)timeSync.now() : 0;
    copyField(current.stopReason, sizeof(current.stopReason), reason);
    saveInfo(current);
    saveState();
    Serial.printf_P(PSTR("Session %u stopped (%s)\n"), current.id, reason);
}

void Logger::addSample(bool ok, float t, float h)
{
    if (!current.active)
        return;

    LogRecord r;
    r.epoch = timeSync.valid() ? (uint32_t)timeSync.now() : 0;
    r.elapsed = elapsedNow();
    if (ok)
    {
        r.t10 = (int16_t)lroundf(constrain(t, -3000.0f, 3000.0f) * 10);
        r.h10 = (uint16_t)lroundf(constrain(h, 0.0f, 100.0f) * 10);
        current.temp.add(t);
        current.hum.add(h);
    }
    else
    {
        r.t10 = INT16_MIN;
        r.h10 = 0xFFFF;
        current.errors++;
    }
    buffer[buffered++] = r;
    if (buffered >= LOG_BUFFER_RECORDS)
        flush();
}

bool Logger::flush()
{
    lastFlush = millis();
    if (!current.active)
        return true;
    if (buffered > 0)
    {
        generation++;
        File f = LittleFS.open(binPath(current.id), "a");
        if (!f)
        {
            Serial.println(F("Logger: cannot open log file"));
            return false;
        }
        size_t bytes = buffered * sizeof(LogRecord);
        size_t written = f.write(reinterpret_cast<const uint8_t *>(buffer), bytes);
        f.close();
        // Keep only whole records that made it to flash.
        uint32_t whole = written / sizeof(LogRecord);
        fileRecords += whole;
        if (whole < buffered)
        {
            memmove(buffer, buffer + whole, (buffered - whole) * sizeof(LogRecord));
            buffered -= whole;
            Serial.println(F("Logger: write error"));
            return false;
        }
        buffered = 0;
    }
    saveInfo(current);
    return true;
}

void Logger::loop()
{
    if (!current.active)
        return;

    // The clock was set after the session started (NTP or browser): work
    // out the start time so all readings get a timestamp.
    if (current.startEpoch == 0 && current.resumes == 0 && timeSync.valid())
    {
        current.startEpoch = (uint32_t)timeSync.now() - (millis() - sessionStartMillis) / 1000;
        saveInfo(current);
    }

    if (buffered > 0 && millis() - lastFlush > LOG_FLUSH_EVERY_MS)
    {
        if (freeBytes() < LOG_MIN_FREE_BYTES)
        {
            Serial.println(F("Logger: storage full, stopping session"));
            stop("storage full");
            return;
        }
        flush();
    }
}

void Logger::forEachSession(std::function<void(const SessionInfo &)> fn)
{
    Dir dir = LittleFS.openDir(LOG_DIR);
    while (dir.next())
    {
        String n = dir.fileName();
        if (!n.endsWith(".json"))
            continue;
        uint32_t id = n.toInt();
        if (id == 0)
            continue;
        if (current.active && id == current.id)
        {
            fn(current); // in-memory copy is the freshest
            continue;
        }
        SessionInfo s;
        if (loadInfo(id, s))
            fn(s);
    }
}

uint32_t Logger::recordCount(uint32_t id)
{
    uint32_t n = 0;
    if (current.active && id == current.id)
        return fileRecords + buffered;
    File f = LittleFS.open(binPath(id), "r");
    if (f)
    {
        n = f.size() / sizeof(LogRecord);
        f.close();
    }
    return n;
}

bool Logger::readRecord(uint32_t id, uint32_t index, LogRecord &out)
{
    bool isActive = current.active && id == current.id;
    uint32_t inFile = isActive ? fileRecords : UINT32_MAX;
    if (isActive && index >= fileRecords)
    {
        uint32_t b = index - fileRecords;
        if (b >= buffered)
            return false;
        out = buffer[b];
        return true;
    }
    File f = LittleFS.open(binPath(id), "r");
    if (!f)
        return false;
    bool ok = index < inFile && f.seek(index * sizeof(LogRecord)) &&
              f.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)) == sizeof(out);
    f.close();
    return ok;
}

bool Logger::remove(uint32_t id, String &error)
{
    if (current.active && id == current.id)
    {
        error = F("Stop the session before deleting it");
        return false;
    }
    if (!LittleFS.exists(infoPath(id)))
    {
        error = F("Session not found");
        return false;
    }
    generation++;
    LittleFS.remove(binPath(id));
    LittleFS.remove(infoPath(id));
    return true;
}

size_t Logger::freeBytes()
{
    FSInfo info;
    LittleFS.info(info);
    return info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
}

size_t Logger::totalBytes()
{
    FSInfo info;
    LittleFS.info(info);
    return info.totalBytes;
}

uint32_t Logger::capacityRecords()
{
    size_t f = freeBytes();
    return f > LOG_MIN_FREE_BYTES ? (f - LOG_MIN_FREE_BYTES) / sizeof(LogRecord) : 0;
}

Logger logger;
