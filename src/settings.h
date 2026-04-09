#pragma once

#include <string>
#include <vector>
#include "ui.h"

#define SETTINGS_PATH "/pocketnomad/settings.json"

struct Settings {
    // Radio (SX1262 LoRa)
    bool  radio_enabled  = false;
    float radio_freq     = 915.0f;  // MHz
    int   radio_sf       = 7;       // spreading factor 7-12
    int   radio_txpower  = 14;      // dBm
    int   radio_cr       = 5;       // coding rate denom (5=4/5 … 8=4/8)

    // WiFi
    bool        wifi_enabled  = false;
    std::string wifi_ssid     = "";
    std::string wifi_password = "";
};

extern Settings settings;

// Load settings from SETTINGS_PATH (silently uses defaults if missing/corrupt)
void loadSettings();

// Persist current settings to SETTINGS_PATH as pretty-printed JSON
void saveSettings();

// Wire up the settings tab.  Pass the items vector of the Settings UiTab —
// it will be rebuilt in-place every time a setting changes.
void initSettings(std::vector<UiItem>& items);
