#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <functional>

// Experiment sessions recorded to LittleFS.
//
//   /logs/<id>.bin   readings, 12 bytes each (LogRecord)
//   /logs/<id>.json  session info (name, notes, start/end time, stats)
//   /sys/logger.json which session is running, so it resumes after a reboot
//
// Readings are buffered in RAM and written in batches to limit flash wear.
// At most LOG_FLUSH_EVERY_MS of data is lost on a power cut.

#define LOG_DIR "/logs"
#define LOG_BUFFER_RECORDS 32
#define LOG_FLUSH_EVERY_MS 120000UL
#define LOG_MIN_FREE_BYTES 16384UL // stop logging below this
#define LOG_NAME_LEN 49
#define LOG_NOTES_LEN 201

struct __attribute__((packed)) LogRecord
{
    uint32_t epoch;   // unix time, 0 if the clock was not set yet
    uint32_t elapsed; // seconds since session start
    int16_t t10;      // temperature x10 (C), INT16_MIN = sensor error
    uint16_t h10;     // humidity x10 (%RH), 0xFFFF = sensor error

    bool ok() const { return t10 != INT16_MIN; }
};
static_assert(sizeof(LogRecord) == 12, "LogRecord must be 12 bytes");

struct RunningStats
{
    float min = NAN, max = NAN;
    double sum = 0;
    uint32_t n = 0;
    void add(float v);
};

struct SessionInfo
{
    uint32_t id = 0;
    char name[LOG_NAME_LEN] = "";
    char notes[LOG_NOTES_LEN] = "";
    uint32_t startEpoch = 0; // 0 = unknown
    uint32_t endEpoch = 0;
    uint32_t intervalMs = 0;
    float tempOffset = 0, humOffset = 0;
    uint16_t resumes = 0;    // times the session continued after a reboot
    uint32_t errors = 0;     // samples with a sensor error
    char stopReason[24] = "";
    RunningStats temp, hum;
    bool active = false;
};

class Logger
{
public:
    void begin();
    void loop();

    bool start(const char *name, const char *notes, String &error);
    void stop(const char *reason = "stopped");
    void addSample(bool ok, float temperature, float humidity);
    // Write buffered readings now (before a restart).
    void flushNow() { flush(); }

    bool active() const { return current.active; }
    const SessionInfo &activeSession() const { return current; }
    uint32_t elapsedNow() const;

    // Session listing and access
    void forEachSession(std::function<void(const SessionInfo &)> fn);
    bool loadInfo(uint32_t id, SessionInfo &out);
    uint32_t recordCount(uint32_t id);
    // Reads record `index` of session `id` (includes unflushed readings).
    bool readRecord(uint32_t id, uint32_t index, LogRecord &out);
    bool remove(uint32_t id, String &error);
    // Readings of session `id` that are already in its file (the rest are
    // still in the RAM buffer). Readers can keep the file open while this
    // and writeGeneration() stay the same.
    uint32_t fileRecordCount(uint32_t id) { return current.active && id == current.id ? fileRecords : recordCount(id); }
    // Changes whenever a log file is written or removed.
    uint32_t writeGeneration() const { return generation; }
    static String binPath(uint32_t id);

    // Storage
    size_t freeBytes();
    size_t totalBytes();
    // Rough number of further readings that fit.
    uint32_t capacityRecords();

private:
    SessionInfo current;
    uint32_t nextId = 1;
    uint32_t sessionStartMillis = 0; // millis() at start (or at boot when resumed)
    uint32_t elapsedAtStartMillis = 0;
    uint32_t fileRecords = 0;        // readings already in the file
    LogRecord buffer[LOG_BUFFER_RECORDS];
    uint8_t buffered = 0;
    uint32_t lastFlush = 0;
    uint32_t generation = 0;
    size_t storageFree = 0, storageTotal = 0;
    uint32_t storageCheckedAt = 0;
    bool storageKnown = false;
    void refreshStorage();

    bool flush();
    bool saveInfo(const SessionInfo &s);
    void saveState();
    static String infoPath(uint32_t id);
};

extern Logger logger;

#endif
