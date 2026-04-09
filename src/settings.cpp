#include "settings.h"
#include "network.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <cmath>

Settings settings;

static std::vector<UiItem>* _items = nullptr;

// ─── Option tables ────────────────────────────────────────────────────────────

struct FloatOpt { const char* label; float value; };
struct IntOpt   { const char* label; int   value; };

static const FloatOpt FREQ_OPTS[] = {
    {"433.175 MHz", 433.175f},
    {"868.1 MHz",   868.1f},
    {"869.525 MHz", 869.525f},
    {"915.0 MHz",   915.0f},
    {"915.2 MHz",   915.2f},
    {"923.2 MHz",   923.2f},
};
static const IntOpt SF_OPTS[] = {
    {"SF7  fastest / short range", 7},
    {"SF8",                        8},
    {"SF9",                        9},
    {"SF10",                      10},
    {"SF11",                      11},
    {"SF12 slowest / long range", 12},
};
static const IntOpt TXPOW_OPTS[] = {
    {"2 dBm",  2}, {"5 dBm",   5}, {"8 dBm",   8},
    {"10 dBm", 10}, {"12 dBm", 12}, {"14 dBm", 14},
    {"17 dBm", 17}, {"20 dBm", 20}, {"22 dBm", 22},
};
static const IntOpt CR_OPTS[] = {
    {"4/5  default", 5},
    {"4/6",          6},
    {"4/7",          7},
    {"4/8  robust",  8},
};

template<size_t N>
static int findFloat(const FloatOpt (&arr)[N], float v) {
    for (int i = 0; i < (int)N; i++)
        if (fabsf(arr[i].value - v) < 0.01f) return i;
    return 0;
}
template<size_t N>
static int findInt(const IntOpt (&arr)[N], int v) {
    for (int i = 0; i < (int)N; i++)
        if (arr[i].value == v) return i;
    return 0;
}
template<typename T, size_t N>
static std::vector<std::string> labels(const T (&arr)[N]) {
    std::vector<std::string> v;
    for (auto& o : arr) v.push_back(o.label);
    return v;
}

// ─── Load / Save ──────────────────────────────────────────────────────────────

void loadSettings() {
    File f = SD.open(SETTINGS_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return; }  // parse error → keep defaults
    f.close();

    settings.radio_enabled  = doc["radio_enabled"]  | false;
    settings.radio_freq     = doc["radio_freq"]      | 915.0f;
    settings.radio_sf       = doc["radio_sf"]        | 7;
    settings.radio_txpower  = doc["radio_txpower"]   | 14;
    settings.radio_cr       = doc["radio_cr"]        | 5;
    settings.wifi_enabled   = doc["wifi_enabled"]    | false;
    settings.wifi_ssid      = doc["wifi_ssid"]       | "";
    settings.wifi_password  = doc["wifi_password"]   | "";
    settings.tcp_host       = doc["tcp_host"]        | "rns.michmesh.net";
    settings.tcp_port       = doc["tcp_port"]        | 7822;
}

void saveSettings() {
    File f = SD.open(SETTINGS_PATH, "w");
    if (!f) return;
    JsonDocument doc;
    doc["radio_enabled"] = settings.radio_enabled;
    doc["radio_freq"]    = settings.radio_freq;
    doc["radio_sf"]      = settings.radio_sf;
    doc["radio_txpower"] = settings.radio_txpower;
    doc["radio_cr"]      = settings.radio_cr;
    doc["wifi_enabled"]  = settings.wifi_enabled;
    doc["wifi_ssid"]     = settings.wifi_ssid;
    doc["wifi_password"] = settings.wifi_password;
    doc["tcp_host"]      = settings.tcp_host;
    doc["tcp_port"]      = settings.tcp_port;
    serializeJsonPretty(doc, f);
    f.close();
}

// ─── Item builder ─────────────────────────────────────────────────────────────

static void rebuildSettings() {
    if (!_items) return;
    _items->clear();

    // ── Wipe Data ──────────────────────────────────────────────────────────
    _items->push_back({
        "Wipe Data", "", nullptr,
        []() {
            if (UiConfirm("Wipe identity?",
                          "Removes identity + messages.\nCannot be undone.")) {
                SD.remove("/pocketnomad/identity");
                UiMsgBox("Done", "Restart the device.");
                ESP.restart();
            }
        }
    });

    // ── Radio ──────────────────────────────────────────────────────────────
    _items->push_back({
        "Radio",
        settings.radio_enabled ? "On" : "Off",
        nullptr,
        []() {
            settings.radio_enabled = !settings.radio_enabled;
            saveSettings();
            rebuildSettings();
        }
    });

    if (settings.radio_enabled) {
        _items->push_back({
            "Frequency",
            FREQ_OPTS[findFloat(FREQ_OPTS, settings.radio_freq)].label,
            nullptr,
            []() {
                int sel = UiPicker("Frequency",
                                   labels(FREQ_OPTS),
                                   findFloat(FREQ_OPTS, settings.radio_freq));
                settings.radio_freq = FREQ_OPTS[sel].value;
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
        _items->push_back({
            "Spread Factor",
            SF_OPTS[findInt(SF_OPTS, settings.radio_sf)].label,
            nullptr,
            []() {
                int sel = UiPicker("Spread Factor",
                                   labels(SF_OPTS),
                                   findInt(SF_OPTS, settings.radio_sf));
                settings.radio_sf = SF_OPTS[sel].value;
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
        _items->push_back({
            "TX Power",
            TXPOW_OPTS[findInt(TXPOW_OPTS, settings.radio_txpower)].label,
            nullptr,
            []() {
                int sel = UiPicker("TX Power",
                                   labels(TXPOW_OPTS),
                                   findInt(TXPOW_OPTS, settings.radio_txpower));
                settings.radio_txpower = TXPOW_OPTS[sel].value;
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
        _items->push_back({
            "Coding Rate",
            CR_OPTS[findInt(CR_OPTS, settings.radio_cr)].label,
            nullptr,
            []() {
                int sel = UiPicker("Coding Rate",
                                   labels(CR_OPTS),
                                   findInt(CR_OPTS, settings.radio_cr));
                settings.radio_cr = CR_OPTS[sel].value;
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
    }

    // ── WiFi ───────────────────────────────────────────────────────────────
    _items->push_back({
        "WiFi",
        settings.wifi_enabled ? "On" : "Off",
        nullptr,
        []() {
            settings.wifi_enabled = !settings.wifi_enabled;
            saveSettings();
            rebuildSettings();
        }
    });

    if (settings.wifi_enabled) {
        _items->push_back({
            "SSID",
            settings.wifi_ssid.empty() ? "(not set)" : settings.wifi_ssid,
            nullptr,
            []() {
                UiPleaseWait();
                WiFi.mode(WIFI_STA);
                WiFi.disconnect();
                delay(100);
                int n = WiFi.scanNetworks();

                std::vector<std::string> opts = {"Enter manually..."};
                for (int i = 0; i < n; i++)
                    opts.push_back(std::string(WiFi.SSID(i).c_str()));
                WiFi.scanDelete();

                int sel = UiPicker("WiFi Network", opts, 0);
                if (sel == 0) {
                    std::string s = UiTextInput("SSID", settings.wifi_ssid.c_str());
                    if (!s.empty()) settings.wifi_ssid = s;
                } else {
                    settings.wifi_ssid = opts[sel];
                }
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
        _items->push_back({
            "Password",
            settings.wifi_password.empty() ? "(not set)" : "***",
            nullptr,
            []() {
                std::string pw = UiPasswordInput("WiFi Password");
                if (!pw.empty()) settings.wifi_password = pw;
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });

        // ── TCP Server ─────────────────────────────────────────────────────
        std::string tcpLabel = settings.tcp_host + ":" + std::to_string(settings.tcp_port);
        _items->push_back({
            "TCP Server",
            tcpLabel,
            nullptr,
            []() {
                std::string h = UiTextInput("RNS TCP Host", settings.tcp_host.c_str());
                if (!h.empty()) settings.tcp_host = h;
                std::string ps = UiTextInput("RNS TCP Port", std::to_string(settings.tcp_port).c_str());
                if (!ps.empty()) {
                    int p = atoi(ps.c_str());
                    if (p > 0 && p < 65536) settings.tcp_port = p;
                }
                saveSettings();
                networkReconfigure();
                rebuildSettings();
            }
        });
    }
}

void initSettings(std::vector<UiItem>& items) {
    _items = &items;
    rebuildSettings();
}
