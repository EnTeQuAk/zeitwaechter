#pragma once

#include "config.h"

// Connect to WiFi and start mDNS + HTTP server.
// Non-blocking — call webserver_loop() in loop().
void webserver_start(TimerConfig& cfg);

// Handle incoming HTTP requests. Call every loop iteration.
void webserver_loop();

// True once WiFi is connected
bool webserver_connected();

// IP address as string (valid after connected)
const char* webserver_ip();

// True if the web UI just triggered a save (cleared after reading)
bool webserver_config_changed();

// True if the web UI just triggered "start timer" (cleared after reading)
bool webserver_start_requested();

// True if running in AP mode (no WiFi configured)
bool webserver_ap_mode();
