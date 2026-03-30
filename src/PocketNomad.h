// this is the manager that ties lora+reticulum to carputer hardware

#include "theme.h"
#include "hal/Cardputer.h"
#include "hal/CardputerSd.h"
#include "hal/CardputerLora.h"
#include "hal/CardputerTask.h"

// shared because this is used for UI
Cardputer c;

class PocketNomad {
public:
  void setup() {
  	c.setup();
  	sd.setup();
  	lora.setup();
  }

  void loop() {
  	c.loop();
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