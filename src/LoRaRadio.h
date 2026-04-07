// this is an abstraction that handles several SPI lora devices
// it could be expanded later for other device-types

#pragma once
#include <RadioLib.h>
#include <SPI.h>
// #include <functional>

struct LoRaConfig {
    float    freq     = 915.0;
    float    bw       = 125.0;
    uint8_t  sf       = 9;
    uint8_t  cr       = 5;
    uint8_t  syncWord = 0x12;
    int8_t   power    = 22;
    uint16_t preamble = 8;
    
    // SPI bus instance (caller initializes and owns it)
    SPIClass* spi = &SPI;
    
    // Module control pins
    int cs   = -1;
    int irq  = -1;
    int rst  = -1;
    int busy = -1;
};

using LoRaReceiveCb = std::function<void(const String& data, float rssi, float snr)>;

class LoRaRadio {
public:
    explicit LoRaRadio(const LoRaConfig& config);
    ~LoRaRadio();

    // Returns true on success
    bool begin();

    // Send a string or raw bytes; returns true on success
    bool send(const String& data);
    bool send(const uint8_t* data, size_t len);

    // Optional: called after radio init to enable the RF path (e.g. IO expander, RF switch)
    void setRfSetup(std::function<void()> cb) { _rfSetup = cb; }

    // Register a callback invoked from update() when a packet arrives
    void onReceive(LoRaReceiveCb cb) { _rxCb = cb; }

    // Call this in loop() to dispatch received packets to the callback
    void update();

    float getRSSI() { return _radio ? _radio->getRSSI() : 0.0f; }
    float getSNR()  { return _radio ? _radio->getSNR()  : 0.0f; }

private:
    LoRaConfig    _config;
    Module*       _mod   = nullptr;
    SX1262*       _radio = nullptr;
    LoRaReceiveCb _rxCb;
    std::function<void()> _rfSetup;

    volatile bool _rxFlag = false;

    // Single-instance ISR trampoline
    static LoRaRadio* _inst;
    static void IRAM_ATTR _isr();
};
