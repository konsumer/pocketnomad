// this is the manager that ties lora+reticulum to carputer hardware

#include <Reticulum.h>
#include <vector>
#include <array>
#include "hal/CardputerSd.h"
#include "hal/CardputerLora.h"

class PocketNomad {
public:
  std::vector<std::array<uint8_t, 16>> peers;

  void setup() {
    sd.setup();

    _loadPeers();
    lora.setup();
    lora.onMessage = [this](uint8_t *data, int len) {
      Reticulum::Packet pkt;
      if (!rns.decodePacket(data, len, pkt)) {
        printf("[msg] decodePacket failed (len=%d)\n", len);
        rns.freePacket(pkt);
        return;
      }
      printf("[msg] packetType=%d dest=%02x%02x%02x%02x...\n",
        pkt.packetType,
        pkt.destinationHash[0], pkt.destinationHash[1],
        pkt.destinationHash[2], pkt.destinationHash[3]);

      if (pkt.packetType == PACKET_ANNOUNCE) {
        Reticulum::Announce announce;
        bool parsed = rns.announceParsePacket(pkt, announce);
        printf("[msg] announceParsePacket=%d valid=%d\n", parsed, parsed ? announce.valid : 0);
        if (parsed && announce.valid) {
          _addPeer(pkt.destinationHash, announce);
        }
        rns.freeAnnounce(announce);
      }

      rns.freePacket(pkt);
    };
  }

  void loop() {
    sd.loop();
    lora.loop();
  }

  int getPeerCount() {
    return (int)peers.size();
  }

private:
  CardputerSd    sd;
  CardputerLora  lora;
  Reticulum      rns;

  void _loadPeers() {
    if (!sd.ok) { printf("[peers] sd not ok\n"); return; }
    std::vector<std::string> names;
    int n = sd.readDir("/pocketnomad/peers", names);
    printf("[peers] readDir found %d entries\n", n);
    for (const auto& name : names) {
      printf("[peers] entry: '%s' (len=%d)\n", name.c_str(), (int)name.size());
      if (name.size() < 32) continue;
      const char* hex = name.c_str() + name.size() - 32;
      std::array<uint8_t, 16> entry;
      char byte[3] = {};
      for (int i = 0; i < 16; i++) {
        byte[0] = hex[i * 2];
        byte[1] = hex[i * 2 + 1];
        entry[i] = (uint8_t)strtol(byte, nullptr, 16);
      }
      peers.push_back(entry);
    }
    printf("[peers] loaded %d peers\n", (int)peers.size());
  }

  void _addPeer(const uint8_t* hash, const Reticulum::Announce& announce) {
    // deduplicate in memory
    for (const auto& p : peers) {
      if (memcmp(p.data(), hash, 16) == 0) return;
    }
    std::array<uint8_t, 16> entry;
    memcpy(entry.data(), hash, 16);
    peers.push_back(entry);

    // persist to SD: /pocketnomad/peers/<hex_hash>
    if (!sd.ok) return;
    sd.createDir("/pocketnomad/peers");

    char path[52];
    snprintf(path, sizeof(path),
      "/pocketnomad/peers/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
      hash[0],  hash[1],  hash[2],  hash[3],
      hash[4],  hash[5],  hash[6],  hash[7],
      hash[8],  hash[9],  hash[10], hash[11],
      hash[12], hash[13], hash[14], hash[15]);

    // write fixed fields (keyPubEncrypt..signature) as one contiguous block,
    // then append appData separately (appData is a pointer, can't cast whole struct)
    const size_t fixedLen = 32 + 32 + 10 + 10 + 32 + 64; // 180 bytes
    printf("[peers] writing to '%s'\n", path);
    sd.write(path, announce.keyPubEncrypt, fixedLen);
    if (announce.appDataLen > 0 && announce.appData) {
      sd.append(path, announce.appData, announce.appDataLen);
    }
  }
};
