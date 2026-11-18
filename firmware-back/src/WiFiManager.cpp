#include <ESP8266WiFi.h>

#include "WiFiManager.h"
#include "configManager.h"
#include "dashboard.h"

WifiManager WiFiManager;

// Timings (ms)
static const uint32_t CONNECT_TIMEOUT = 20000;   // first attempt / new credentials
static const uint32_t LOST_PORTAL_AFTER = 120000; // open portal after 2 min outage
static const uint32_t PORTAL_RETRY_EVERY = 60000; // retry saved network while portal is up

void WifiManager::begin()
{
    WiFi.persistent(true); // SDK stores the station credentials in flash
    WiFi.setAutoReconnect(true);
    char hostname[16];
    snprintf_P(hostname, sizeof(hostname), PSTR("wot-%06x"), ESP.getChipId());
    WiFi.hostname(hostname);
    WiFi.mode(WIFI_STA);

    applySavedStationIP();

    if (WiFi.SSID().length() > 0)
    {
        Serial.printf_P(PSTR("Connecting to saved network \"%s\"...\n"), WiFi.SSID().c_str());
        WiFi.begin();
        setState(State::Connecting);
    }
    else
    {
        Serial.println(F("No saved network"));
        setState(State::Idle);
        startCaptivePortal();
    }
}

void WifiManager::setState(State s)
{
    state = s;
    stateSince = millis();
}

bool WifiManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

void WifiManager::applyStationIP(const IPAddress &ip, const IPAddress &gw, const IPAddress &sub, const IPAddress &dns)
{
    if (ip.isSet())
    {
        Serial.println(F("Using static IP"));
        WiFi.config(ip, gw, sub, dns);
    }
    else
    {
        WiFi.config(IPAddress(), IPAddress(), IPAddress()); // back to DHCP
    }
}

void WifiManager::applySavedStationIP()
{
    const networkData &n = configManager.internal;
    applyStationIP(IPAddress(n.ip), IPAddress(n.gw), IPAddress(n.sub), IPAddress(n.dns));
}

void WifiManager::forget()
{
    forgetRequested = true;
}

void WifiManager::setNewWifi(const String &ssid, const String &pass)
{
    newSSID = ssid;
    newPass = pass;
    newIp = newGw = newSub = newDns = IPAddress();
    newWifiRequested = true;
}

void WifiManager::setNewStaticWifi(const String &ssid, const String &pass, const String &ip,
                                   const String &sub, const String &gw, const String &dns)
{
    newSSID = ssid;
    newPass = pass;
    newIp.fromString(ip);
    newSub.fromString(sub);
    newGw.fromString(gw);
    newDns.fromString(dns);
    newWifiRequested = true;
}

void WifiManager::startCaptivePortal()
{
    if (inCaptivePortal)
        return;

    IPAddress ip, gw, sub;
    ip.fromString(configManager.deviceConfigData.deviceIP);
    gw.fromString(configManager.deviceConfigData.deviceGw);
    sub.fromString(configManager.deviceConfigData.deviceSub);
    String ssid = configManager.portalSSID();

    // AP + station, so the station side can keep trying the saved network.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(ip, gw, sub);
    if (!WiFi.softAP(ssid.c_str(), configManager.deviceConfigData.devicePass))
    {
        // Should not happen since the password is validated, but never leave
        // the device unreachable: fall back to the default password.
        Serial.println(F("softAP failed, retrying with default password"));
        WiFi.softAP(ssid.c_str(), deviceDefaults.devicePass);
    }

    // Redirect every DNS name to the portal.
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    inCaptivePortal = true;
    lastRetry = millis();
    Serial.printf_P(PSTR("Captive portal \"%s\" open at %s\n"), ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void WifiManager::stopCaptivePortal()
{
    if (!inCaptivePortal)
        return;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    inCaptivePortal = false;
    Serial.println(F("Captive portal closed"));
}

void WifiManager::handleForget()
{
    forgetRequested = false;
    newWifiRequested = false;
    Serial.println(F("Forgetting WiFi network"));
    WiFi.disconnect(); // with persistent(true) this also erases the stored credentials
    configManager.setStationStaticIP(0, 0, 0, 0);
    applySavedStationIP();
    setState(State::Idle);
    startCaptivePortal();
}

void WifiManager::handleNewWifi()
{
    newWifiRequested = false;

    // Remember the current network in case the new one fails.
    oldSSID = WiFi.SSID();
    oldPass = WiFi.psk();

    Serial.printf_P(PSTR("Trying new network \"%s\"...\n"), newSSID.c_str());
    applyStationIP(newIp, newGw, newSub, newDns);
    WiFi.begin(newSSID.c_str(), newPass.c_str());
    setState(State::TryingNew);
}

void WifiManager::loop()
{
    if (inCaptivePortal)
        dnsServer.processNextRequest();

    if (forgetRequested)
        handleForget();
    if (newWifiRequested)
        handleNewWifi();

    bool connected = isConnected();
    dash.data.networkStatus = connected;
    uint32_t now = millis();

    switch (state)
    {
    case State::Idle:
        break;

    case State::Connecting:
        if (connected)
        {
            Serial.printf_P(PSTR("Connected. Open the dashboard at http://%s\n"), WiFi.localIP().toString().c_str());
            setState(State::Connected);
        }
        else if (now - stateSince > CONNECT_TIMEOUT)
        {
            Serial.println(F("Could not connect to saved network, opening portal (will keep retrying)"));
            setState(State::Lost);
            startCaptivePortal();
        }
        break;

    case State::Connected:
        if (!connected)
        {
            Serial.println(F("WiFi connection lost, reconnecting..."));
            setState(State::Lost);
        }
        break;

    case State::Lost:
        if (connected)
        {
            Serial.printf_P(PSTR("WiFi reconnected: %s\n"), WiFi.localIP().toString().c_str());
            setState(State::Connected);
            break;
        }
        if (!inCaptivePortal && now - stateSince > LOST_PORTAL_AFTER)
        {
            Serial.println(F("Still offline, opening portal"));
            startCaptivePortal();
        }
        // While the portal is open, retry now and then, but only when no one
        // is connected to the portal (a retry scan disturbs the AP channel).
        if (inCaptivePortal && WiFi.softAPgetStationNum() == 0 && now - lastRetry > PORTAL_RETRY_EVERY)
        {
            lastRetry = now;
            WiFi.begin();
        }
        break;

    case State::TryingNew:
        if (connected)
        {
            Serial.printf_P(PSTR("New network connected: %s\n"), WiFi.localIP().toString().c_str());
            configManager.setStationStaticIP(newIp.v4(), newGw.v4(), newSub.v4(), newDns.v4());
            setState(State::Connected);
        }
        else if (now - stateSince > CONNECT_TIMEOUT)
        {
            Serial.println(F("New network failed"));
            applySavedStationIP();
            if (oldSSID.length() > 0)
            {
                Serial.println(F("Restoring previous network"));
                WiFi.begin(oldSSID.c_str(), oldPass.c_str());
                setState(State::Lost);
                startCaptivePortal(); // stays reachable while it reconnects
            }
            else
            {
                WiFi.disconnect();
                setState(State::Idle);
                startCaptivePortal();
            }
        }
        break;
    }

    // Close the portal once the station link is up and nobody is using it.
    if (inCaptivePortal && state == State::Connected && WiFi.softAPgetStationNum() == 0)
        stopCaptivePortal();
}

String WifiManager::SSID()
{
    return inCaptivePortal && !isConnected() ? configManager.portalSSID() : WiFi.SSID();
}

String WifiManager::getAddress()
{
    return inCaptivePortal && !isConnected() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
