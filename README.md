# WoT Firmware for ESP8266

WoT firmware project is implemented as a starting point for using WoT-based devices in real IoT projects. This firmware is providing some basic IoT functionalities such as HTTP web server, MQTT-based event publisher, DHT11 sensor management, interactive web interface, network manager, configuration manager, live dashboard and Over-The-Air updates. Also, the UI implementation of this firmware is attached to this repository to help developers to customize or modify for their projects.

![Firmware-Dashboard](https://github.com/IDAS-Labratory/Web-of-Things-Firmware/blob/master/doc/UI/UI.png?raw=true)

## Table of Contents
- [Requirements](#Requirements)
     - [Hardware Requirements](#Hardware-Requirements)
     - [Software Requirements](#Software-Requirements)
- [Installation](#Installation)
     - [Hardware Setup](#Hardware-Setup)
     - [Software Setup](#Software-Setup)
     - [Configuration](#Configuration)
- [Software Architecture and Design Approach](#Sotware-Architecture-and-Design-Approach)
     - [Software Architecture](#Software-Architecture)
     - [Module Structure](#Module-Structure)
- [Learn More](#Learn-More)


## Requirements

### Hardware Requirements
* [Wemos D1](http://www.esp8266learning.com/wemos-d1-esp8266-based-board.php), mini or any type of [ESP8266](https://www.espressif.com/en/products/modules/esp8266) board.

* [DHT11](https://www.adafruit.com/product/386) sensor

### Software Requirements

* [PlatformIO](https://platformio.org/), to develop and deploy firmware on ESP8266. It is recommended to install PlatformIO as a extensions on VSCode. Read more about installing PlatformIO [here](https://platformio.org/install/ide?install=vscode).

## Installation

Now, you can clone this repository and begin to setup your hardware and software.

### Hardware Setup

Follow this instruction according [schematic](#Hardware-Schematic) image to setup the hardware.

1. Connect 5v Vcc and GND pins of Wemos D1 to breadboard power rails according to [Wemos D1 pinout](https://www.wemos.cc/en/latest/d1/d1_mini.html) 

2. Connect GPIO 5 of D1 to data pin (pin 2) DHT11.
>>__Notice__: Do not forget to connect a pull-up resistor to data pin. Otherwise, your sensor doesn’t work properly.

3. Connect DHT11 VCC and GND pins to breadboard power rails.

![Hardware-Schematic](https://github.com/IDAS-Labratory/Web-of-Things-Firmware/blob/master/doc/diagrams/Schematic/Firmware%20Sketch_bb.png?raw=true)


### Software Setup

1. Firstly, clone this repository on your PC.
2. Open main directory of repository and then open `/firmware-back`.
3. Now click right through this directory and select the VScode to open the program files.
>>__Note__: You should open directory that contains `platformio.ini` file in order to PlatformIO can be activated on your VSCode.
4. Connect your Wemos D1 to your PC by USB cable.
5. At this step you should build the program and then upload it on the Wemos, according below.

### Configuration
Default values live in `/firmware-back/src/default/config.cpp`. Settings changed from the web UI are saved as JSON in the device file system (`/sys/config.json`), so they survive restarts and firmware updates. On first boot after upgrading from v1.0, settings stored by the old firmware in EEPROM are imported automatically.

The sensor model and GPIO are read from the saved settings (defaults: DHT11 on GPIO 5).

#### First start

1. If the device cannot join a WiFi network, it opens an access point named `WoT-XXXXXX` (unique per device), password `12345678`. Connect to it and open `http://192.168.4.1`.
2. Log in with user `admin` and password `admin`.
3. Change the admin password straight away:
   ```
   curl -u admin:admin -H "Content-Type: application/json" \
        -d '{"current":"admin","password":"your-new-password"}' \
        http://<device-ip>/api/config/admin/set
   ```
4. Set your WiFi network on the Network page. If the network is not available at boot or drops later, the device keeps retrying in the background and opens the access point after a while so it stays reachable.

#### Security

- Every page and API endpoint requires the admin login (HTTP Basic auth).
- Requests that change state are rejected when they come from another website (cross-site request protection), so a web page you visit cannot reset or reflash the device.
- Passwords are never returned by the API. Leave a password field empty to keep the current one.
- The access point password must be 8 to 63 characters.

Basic auth is not encrypted, so use the device on a trusted network.

The user interface of WoT firmware is developed on [Preact JS](https://preactjs.com/) framework. To change UI dashboard, you should develop `/firmware-front` and then applying modifications by this command `npm run build`. New `html.h` file is automatically replaced by previous version. While developing the UI on another origin, build the firmware with `-DWOT_ENABLE_CORS` (see `platformio.ini`); do not ship that build.

## Experiment logger

Open `http://<device-ip>/` to use the logger page. It shows:

- the current temperature and humidity
- a **Start recording** form (name, notes, sample interval) and a **Stop** button
- a live chart of the running session, with min / average / max
- every saved session, with **Chart**, **CSV** download and **Delete**
- calibration offsets and time settings

The original settings pages are still at `/config`, `/network` and `/setting`.

### How recording works

- Readings go to the device's flash (`/logs/<id>.bin`, 12 bytes each). The 2 MB file system holds about 160,000 readings. That is roughly 18 days at a 10 s interval, or 110 days at 60 s. The page shows how much space is left.
- Readings are written in batches, every 32 readings or 2 minutes, to limit flash wear. A power cut loses at most the last batch.
- If the device restarts during a session, recording continues in the same session after boot.
- A failed sensor read is saved as a `sensor_error` row, so gaps show up in the data instead of fake values.
- Logging stops by itself when the flash is almost full.

### Time

- With internet access the clock is set by NTP (`pool.ntp.org` by default).
- Without internet, opening the logger page sets the device clock from your browser, including your time zone's UTC offset. Open the page once before starting a session so readings get real timestamps. If the clock is set later, timestamps are filled in from the session start.
- For daylight saving time, enter a POSIX time zone, for example `CET-1CEST,M3.5.0,M10.5.0/3`.

### CSV format

```
time_local,unix_time,elapsed_s,temperature_c,humidity_pct,status
2026-11-18T11:50:05+03:30,1794990005,0,24.7,41.0,ok
2026-11-18T11:50:15+03:30,1794990015,10,,,sensor_error
```

`time_local` is empty when the clock was not set. Calibration offsets are applied before readings are saved, and the offsets used are stored with each session.

### Logger API

HTTP Verb |API                              | Request Body Value   | Description
----------|---------------------------------|----------------------|------------
GET       |/api/logger/status               | {}                   | Current reading, clock, storage, running session
POST      |/api/session/start               | { "name": "Test 1", "notes": "", "rate": 10000 } | Start a session (`rate` in ms is optional)
POST      |/api/session/stop                | {}                   | Stop the running session
GET       |/api/sessions                    | {}                   | List sessions with stats
GET       |/api/session/data?id=N&from=0&step=1&max=2000 | {}       | Readings as JSON rows `[elapsed_s, unix_time, temp, hum]`
GET       |/api/session/csv?id=N            | {}                   | Download a session as CSV
POST      |/api/session/delete?id=N         | {}                   | Delete a stopped session
GET       |/api/time                        | {}                   | Clock status
POST      |/api/time/set                    | { "epoch": 1794990005, "offset": 210 } | Set the clock (offset = minutes east of UTC)
POST      |/api/config/time/set             | { "ntp": "pool.ntp.org", "tz": "UTC0" } | NTP server and POSIX time zone
POST      |/api/config/calibration/set      | { "temp": -0.5, "hum": 2.0 } | Calibration offsets (±10 °C, ±20 %RH)

After editing `firmware-back/web/logger.html`, run `python3 firmware-back/tools/embed_html.py` to regenerate `src/default/logger_html.h`.

## APIs Endpoint

### REST APIs

All endpoints require HTTP Basic auth (user `admin`). JSON endpoints require `Content-Type: application/json`. Invalid values are rejected with HTTP 400 and a message.

HTTP Verb |API                            | Request Body Value   | Description   
----------|-------------------------------| -------------------- |------------
GET       |/api/index                     | {}                   | Index
POST      |/api/restart                   | {}                   | Restart device
POST      |/api/reset                     | {}                   | Reset device to default values
GET       |/api/config/get                | {}                   | Get configuration data
POST      |/api/config/device/set         | { "name": "WoT", "ssid": "WoT AP", "pass":"wot1234567"} | Set device configuration
POST      |/api/config/device/setStatic   | { "name": "WoT", "ssid": "WoT AP", "pass": "wot1234567", "ip": "192.168.1.1", "subnet": "255.255.255.0", "gateway": "192.168.1.1" }             | Set captive portal configuration by static address 
POST      |/api/config/sensor/set         | { "rate": 5000 }     | Set sample interval in ms (2000 to 3600000)
POST      |/api/config/broker/set         | { "status": true, "ip": "192.168.1.100", "port": 1883, "topic": "/temperature", "qos": 0 }                                                                | Set MQTT broker (`ip` may also be a hostname); applied without restart
POST      |/api/config/admin/set          | { "current": "admin", "password": "new-password" } | Change the admin password (8 to 64 characters)
GET       |/api/network/get               | {}                   | Get captive portal network status
POST      |/api/network/set               | { "ssid": "IDAS-Lab", "pass": "password" }    | Set network connection
POST      |/api/network/setStaticIP       | { "ip": "192.168.1.90", "subnet": "255.255.255.0", "gateway": "192.168.1.1", "dns": "8.8.8.8", "ssid": "IDAS-Lab", "pass": "password" }       | Set network connection by static address
GET       |/api/dashboard/get             | {}                   | Get dashboard information
GET       |/api/files/get                 | {}                   | List uploaded files and flash usage
POST      |/upload                        | multipart file       | Upload a file (name: letters, digits, `.`, `_`, `-`; max 31 chars)
GET       |/download/{filename}           | {}                   | Download an uploaded file
POST      |/api/files/remove?filename=... | {}                   | Delete an uploaded file
POST      |/api/update?filename=...       | {}                   | Flash an uploaded firmware `.bin`
GET       |/api/update-status             | {}                   | Firmware update status (254 busy, 1 done, 0 failed)

### MQTT API

Topic        | QoS | Message
-------------|-----|--------
/temperature | 0   | { "name": "WoT Device", "timestamp": 9801423, "value": { "temperature": 24.7, "humidity": 41.0 } }

Messages are published once per sample, only when the sensor reading is valid. `timestamp` is milliseconds since boot.

## Sotware Architecture and Design Approach

The architecture of WoT firmware is designed over the WoT-Middleware architecture that is proposed on previous project, [WoT-Middleware Platform](https://github.com/IDAS-Labratory/Web-of-Things-Middleware-Platform), however, the firmware architecture is implemented, pretty much different than WoT-Middleware platform, according to its micro programming nature. Therefore, WoT firmware architecture designed and implemented follow the __Layered Architecture Style__ beside __Object-Oriented__ architecture over the Arduino framework. 

![WoT-Firmware-Architecture](https://github.com/IDAS-Labratory/Web-of-Things-Firmware/blob/master/doc/diagrams/System/Architecture.png?raw=true)

### Software-Architecture

The firmware architecture is designed in three main layers, including Application, Middleware, and Things layer.

#### Application Layer

This firmware provides some basic REST APIs and MQTT Topic in order to enable developers to use WoT device functionalities in other applications development.  

#### Middleware Layer

The middleware layer is responsible for three major tasks as _Providing Services_, _Implementing Functionalities_ and _Connecting to Hardware_. So, the three parts of designed middleware are in charge of each one of them. we will describe each part in the following.

1. The publisher part is carried out the _Providing Services_ by implementing both MQTT Publisher and HTTP Web Server.

2. The main functionalities of firmware are implemented in major part of the middleware layer. Live dashboard, WiFi manager, sensor manager, Over-The-Air upgrading and configuration management are the main functionalities which are implemented in this layer.

3. To run and work with sensor module and ESP flash memory, we leverage two basic Arduino libraries as the [DHT sesnor library](https://github.com/adafruit/DHT-sensor-library) and [Little Fail-Safe Filesystem](https://github.com/littlefs-project/littlefs) in middleware layer to connect to DHT sensor and flash memory hardware, respectively.


#### Things Layer

This layer contains our internal and external hardware, consist of DHT11 sensor as a external part and ESP flash memory as an internal one. 

### Module Structure

The classes functionalities and responsibility are detailed in the following.

- `src/main.cpp`: Firstly, it calls begin functions of all sub-systems individually to setup them and then running sub-systems life-cycle in the loop function. Each new sensor sample is pushed to the dashboard and the MQTT publisher from here.

- `src/webServer.cpp`: This class by leveraging ESPAsyncWebServer library, as a web server, provides some Restful APIs to represent device functionalities on the Web. Users control and monitor WoT device through GUI dashboard or these APIs independently.

- `src/publisher.cpp`: This class uses AsyncMqttClient library to enable WoT device to publish its data through MQTT protocol.

- `src/configManager.cpp`: Loads and validates settings from `/sys/config.json` (falling back to defaults), and saves changes from the main loop.
- `src/WiFiManager.cpp`: Non-blocking network management: station connection, background reconnect, and the captive portal.
- `src/sensor.cpp`: Creating sensor object from DHT library, reading data from sensor and storing them in temporary memory are main activities of this class.  
- `src/dashboard.cpp`: This class is responsible for preparing dashboard items and representing them by WebSocket connection into firmware UI.
- `src/updater.cpp`: WoT firmware has been able to Over-The-Air update functionality by this class.
- `src/logger.cpp`: Experiment sessions: start/stop, batched writes to flash, resume after reboot, stats, reading back for chart and CSV.
- `src/timeSync.cpp`: Wall-clock time from NTP or the browser, and the time zone.
- `src/system.cpp`: Runs restart and factory reset requests from the main loop, after the HTTP reply is sent.

```
firmware-back/src/
     default/
          config.cpp
          config.h
          html.h
          logger_html.h   (generated from web/logger.html)
     configManager.cpp
     configManager.h
     logger.cpp
     logger.h
     dashboard.cpp
     dashboard.h
     main.cpp
     publisher.cpp
     publisher.h
     sensor.cpp
     sensor.h
     system.cpp
     system.h
     timeSync.cpp
     timeSync.h
     updater.cpp
     updater.h
     webServer.cpp
     webServer.h
     WiFiManager.cpp
     WiFiManager.h
```

## Learn-More

- This project is build and run over the [ESP8266 IoT framework](https://github.com/maakbaas/esp8266-iot-framework). Please find out this repository to learn more about implementation details.

- Build the firmware with PlatformIO (`pio run`) and flash it with `pio run -t upload`. Prebuilt binaries are no longer kept in the repository.
