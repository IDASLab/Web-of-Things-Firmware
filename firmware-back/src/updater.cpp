#include "updater.h"
#include <LittleFS.h>

void LittleFSUpdater::requestStart(const String &filenameIn)
{
    status = 254;
    filename = filenameIn;
    requestFlag = true;
}

void LittleFSUpdater::loop()
{
    if (requestFlag)
    {
        requestFlag = false;
        flash(filename);
    }
}

void LittleFSUpdater::flash(const String &path)
{
    bool success = false;
    File file = LittleFS.open(path, "r");

    if (!file)
    {
        Serial.println(F("Update: failed to open file"));
    }
    else
    {
        size_t fileSize = file.size();
        // ESP8266 application images start with magic byte 0xE9.
        int magic = file.peek();
        if (fileSize == 0 || magic != 0xE9)
        {
            Serial.println(F("Update: not an ESP8266 firmware image"));
        }
        else if (!Update.begin(fileSize))
        {
            Serial.printf_P(PSTR("Update: cannot start (error %u)\n"), Update.getError());
        }
        else
        {
            Serial.println(F("Update: flashing..."));
            size_t written = Update.writeStream(file);
            if (written != fileSize)
            {
                Serial.printf_P(PSTR("Update: wrote %u of %u bytes\n"), written, fileSize);
                Update.end(false); // abort, keep current firmware
            }
            else if (Update.end())
            {
                Serial.println(F("Update: success, restart to run the new firmware"));
                success = true;
            }
            else
            {
                Serial.printf_P(PSTR("Update: failed (error %u)\n"), Update.getError());
            }
        }
        file.close();
    }

    status = success ? 1 : 0;
}

LittleFSUpdater updater;
