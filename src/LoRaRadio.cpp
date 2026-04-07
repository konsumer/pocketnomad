#include "LoRaRadio.h"

LoRaRadio* LoRaRadio::_inst = nullptr;

void IRAM_ATTR LoRaRadio::_isr() {
    if (_inst) _inst->_rxFlag = true;
}

LoRaRadio::LoRaRadio(const LoRaConfig& config) : _config(config) {
    _inst = this;
}

LoRaRadio::~LoRaRadio() {
    delete _radio;
    delete _mod;
    if (_inst == this) _inst = nullptr;
}

bool LoRaRadio::begin() {
    // SPI bus is initialized by the caller; just attach to it
    _mod = new Module(_config.cs, _config.irq, _config.rst, _config.busy, *_config.spi);
    _radio = new SX1262(_mod);

    int state = _radio->begin(
        _config.freq, _config.bw, _config.sf, _config.cr,
        _config.syncWord, _config.power, _config.preamble,
        /* tcxoVoltage */ 3.0, /* useRegulatorLDO */ true
    );
    if (state != RADIOLIB_ERR_NONE) return false;

    _radio->setCurrentLimit(140);

    if (_rfSetup) _rfSetup();

    _radio->setPacketReceivedAction(_isr);

    state = _radio->startReceive();
    return state == RADIOLIB_ERR_NONE;
}

bool LoRaRadio::send(const String& data) {
    return send((const uint8_t*)data.c_str(), data.length());
}

bool LoRaRadio::send(const uint8_t* data, size_t len) {
    if (!_radio) return false;
    // Transmit blocks until done, then re-enters receive
    int state = _radio->transmit(data, len);
    if (state != RADIOLIB_ERR_NONE) return false;
    _radio->startReceive();
    return true;
}

void LoRaRadio::update() {
    if (!_rxFlag) return;
    _rxFlag = false;

    size_t len = _radio->getPacketLength();
    uint8_t buf[255];
    int state = _radio->readData(buf, len);
    if (state == RADIOLIB_ERR_NONE && _rxCb) {
        _rxCb(String((const char*)buf, len), _radio->getRSSI(), _radio->getSNR());
    }
    // Re-arm receive in case readData exited it
    _radio->startReceive();
}
