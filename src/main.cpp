#include "theme.h"
#include "Counter.h"

#include "PocketNomad.h"

PocketNomad nomad;

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

void setup(void) {
  nomad.setup();
  tabs[currentTab]->setup();
}

void loop(void) {
  nomad.loop();

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
  c.fillRect(c.width() - 24, 4, 20, 10, THEME_BATTERY_BG);
  c.fillRect(c.width() - 23, 5, 18 * (nomad.level/100), 8, THEME_BATTERY_FG);
  c.setTextColor(THEME_BATTERY_FG);
  c.setTextSize(1);
  c.setCursor(c.width() - 32, 4);
  if (nomad.chargingState == 0) {
    c.print("D"); // discharging
  } else if (nomad.chargingState == 1) {
    c.print("C"); // charging
  } else {
    c.print("?");
  }
}

