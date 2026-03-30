// this is the manager that ties lora+reticulum to carputer hardware

#include "hal/CardputerSd.h"
#include "hal/CardputerLora.h"

class PocketNomad {
public:
  void setup() {
  	sd.setup();
  	lora.setup();
    lora.onMessage = [this](uint8_t *data, int len) {
      // parse message
      // if ANNOUNCE, update peers & save
      // if MESSAGE, check if to me & save
    };
  }

  void loop() {
  	sd.loop();
  	lora.loop();
  }

  int getPeerCount() {
  	return 0;
  }
private:
	CardputerSd    sd;
	CardputerLora  lora;
};