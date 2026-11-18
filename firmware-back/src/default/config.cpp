#include <Arduino.h>
#include "config.h"

const deviceData deviceDefaults = {
	"WoT Device",
	"",            // empty = "WoT-<chip id>", unique per device
	"12345678",
	"192.168.4.1",
	"192.168.4.1",
	"255.255.255.0"
};

const sensorData sensorDefaults = {
	11,   // DHT11
	5,    // GPIO 5 (D1)
	5000  // 5 s
};

const brokerData brokerDefaults = {
	false,
	"",
	1883,
	"/temperature",
	0,
};

// Change this after first login (POST /api/config/admin/set).
const adminData adminDefaults = {
	"admin"
};
