#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>
#include <DNSServer.h>

// Non-blocking WiFi manager.
//  - Connects to the saved network at boot.
//  - If that fails, opens the captive portal (access point) but keeps
//    retrying the saved network in the background, and closes the portal
//    again once the station link is back and nobody is using the portal.
//  - If the link drops at runtime, the SDK reconnects automatically; after
//    a long outage the portal opens too, so the device stays reachable.
// Web handlers only queue requests; all WiFi calls happen in loop().
class WifiManager
{
public:
    void begin();
    void loop();

    // Queue requests (safe to call from web server callbacks).
    void forget();
    void setNewWifi(const String &newSSID, const String &newPass);
    void setNewStaticWifi(const String &newSSID, const String &newPass, const String &newIp,
                          const String &newSub, const String &newGw, const String &newDns);

    bool isCaptivePortal() const { return inCaptivePortal; }
    bool isConnected() const;
    String SSID();
    String getAddress();

private:
    enum class State
    {
        Idle,       // no saved network
        Connecting, // first connection attempt after boot
        Connected,
        Lost,       // was connected, SDK is reconnecting
        TryingNew,  // testing credentials sent from the UI
    };

    State state = State::Idle;
    DNSServer dnsServer;
    bool inCaptivePortal = false;
    uint32_t stateSince = 0;
    uint32_t lastRetry = 0;

    // Pending requests from the web server
    bool forgetRequested = false;
    bool newWifiRequested = false;
    String newSSID, newPass;
    IPAddress newIp, newGw, newSub, newDns;

    // Previous network, restored if new credentials fail
    String oldSSID, oldPass;

    void setState(State s);
    void applyStationIP(const IPAddress &ip, const IPAddress &gw, const IPAddress &sub, const IPAddress &dns);
    void applySavedStationIP();
    void startCaptivePortal();
    void stopCaptivePortal();
    void handleForget();
    void handleNewWifi();
};

extern WifiManager WiFiManager;

#endif
