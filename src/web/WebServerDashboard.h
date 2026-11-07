#ifndef WEB_SERVER_DASHBOARD_H
#define WEB_SERVER_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>

#include "AdminSession.h"

// Webserver fuer das Dashboard: statische Web-UI aus LittleFS, REST-API (Konfiguration, Backup, Push, MQTT,
// WLAN), WebSocket-Telemetrie. Die Update-Routen (/api/update/*) liegen im UpdateService.
class WebServerDashboard {
public:
    static WebServerDashboard& getInstance();

    void init();
    void start();
    void broadcastSensorData();
    void broadcastWifiData();
    void broadcastUptime();
    void broadcastMqttState();

private:
    WebServerDashboard();
    void setupRoutes();
    bool requireAdmin(AsyncWebServerRequest *request);

    AsyncWebServer server_;
    AsyncWebSocket ws_;
    AdminSession admin_;
};

#endif // WEB_SERVER_DASHBOARD_H
