#include "theme.h"
#include "Counter.h"
#include "hal/Cardputer.h"
#include "hal/CardputerTask.h"

#include "PocketNomad.h"

Cardputer c;
CardputerTaskManager tasks;
PocketNomad nomad;

// Draws an upward pointing triangle
void drawScrollUp(int x, int y, int w = 6, int h = 6) {
  c.fillTriangle(x + w/2, y, x, y + h, x + w, y + h, THEME_SCROLL);
}

// Draws a downward pointing triangle
void drawScrollDown(int x, int y, int w = 6, int h = 6) {
  c.fillTriangle(x, y, x + w, y, x + w/2, y + h, THEME_SCROLL);
}

#include "Tab.h"
#include "TabHome.h"
#include "TabMessages.h"
#include "TabPeers.h"

std::vector<Tab*> tabs = {
  new TabHome(),
  new TabMessages(),
  new TabPeers(),
};
Counter currentTab(tabs.size());

int batteryLevel = 0;
int chargingState = 3;
CardputerTask taskBattery;

void setup(void) {
  c.setup();
  nomad.setup();
  tabs[currentTab]->setup();
  tasks.setup();

  // setup task to periodically update battery indicators
  taskBattery.start("battery", 2048, []() {
    chargingState = c.chargingState();
    batteryLevel = c.batteryLevel();
    CardputerTask::delay(2000);
  }, &tasks);
}

void loop(void) {
  c.loop();
  nomad.loop();
  tasks.update();

  tabs[currentTab]->update();

  // draw tab bar
  c.setTextSize(1.0);
  int count = tabs.size();
  int w = c.width() / count;
  int h = 16;
  int y = c.height() - h;
  for (int i = 0; i < count; i++) {
    int x = i * w;
    bool active = i == currentTab;
    c.fillRoundRect(x + 1, y, w - 2, h - 2, 3, active ? THEME_TAB_ACTIVE_BG : THEME_TAB_BG);
    c.setTextColor(active ? THEME_TAB_ACTIVE_FG : THEME_TAB_FG);
    c.drawCenterString(tabs[i]->name, x + w / 2, y + 4);
  }

  // left/right switches tabs (edge-triggered)
  bool left  = c.isKeyPressed(KEY_LEFT);
  bool right = c.isKeyPressed(KEY_RIGHT);
  static bool prevLeft  = false;
  static bool prevRight = false;
  if (left && !prevLeft)   { --currentTab; tabs[currentTab]->setup(); }
  if (right && !prevRight) { ++currentTab; tabs[currentTab]->setup(); }
  prevLeft  = left;
  prevRight = right;

  // show battery indicator
  c.fillRoundRect(c.width() - 24, 4, 20, 10, 3, THEME_BATTERY_BG);
  c.fillRoundRect(c.width() - 23, 5, 18 * (batteryLevel/100.0f), 8, 3, THEME_BATTERY_FG);
  c.setTextColor(THEME_BATTERY_FG);
  c.setTextSize(1);
  c.setCursor(c.width() - 32, 5);
  if (chargingState == 0) {
    c.print("D"); // discharging
  } else if (chargingState == 1) {
    c.print("C"); // charging
  } else {
    c.print("?");
  }
}

