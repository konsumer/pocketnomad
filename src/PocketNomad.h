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
  int  chargingState = 2;  // 0=discharging, 1=charging, 2=unknown
  int  level         = -1;
  int  voltage       = 0;

  void setup() {
  	c.setup();
  	sd.setup();
  	lora.setup();
    tasks.setup();

    // setup task to periodically update battery indicators
    taskBattery.start("battery", 2048, [this]() {
      _read();
      CardputerTask::delay(2000);
    }, &tasks);
  }

  void loop() {
  	c.loop();
  	sd.loop();
  	lora.loop();
    tasks.update();
  }

  int getPeerCount() {
  	return 0;
  }
private:
	CardputerSd    sd;
	CardputerLora  lora;
  CardputerTaskManager tasks;
  CardputerTask taskBattery;

  void _read() {
    chargingState = c.chargingState();
    level         = c.batteryLevel();
    voltage       = c.batteryVoltage();
  }
};