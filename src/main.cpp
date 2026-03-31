#include "theme.h"
#include "Counter.h"
#include "hal/Cardputer.h"
#include "hal/CardputerTask.h"
#include "PocketNomad.h"

Cardputer c;
PocketNomad nomad;
bool showTabs = true;

#include "Tab.h"
#include "ConvView.h"

ConvView conv;

// Draw a scrollable list of strings. selected is the highlighted index.
// Returns true if ENTER was pressed on the selected item.
// scroll is updated in place to keep selected visible.
bool drawList(const std::vector<std::string>& items, int& selected, int& scroll) {
  const int LINE_W = 6 * 32 + 2;
  const int LINE_H  = 10;
  const int visible = (c.height() - HEIGHT_FOOTER - HEIGHT_HEADER) / LINE_H;
  const int count   = (int)items.size();

  bool up    = c.isKeyPressed(KEY_UP);
  bool down  = c.isKeyPressed(KEY_DOWN);
  bool enter = c.isKeyPressed(KEY_ENTER);
  static bool prevUp = false, prevDown = false, prevEnter = false;
  if (up    && !prevUp    && selected > 0)         selected--;
  if (down  && !prevDown  && selected < count - 1) selected++;
  prevUp    = up;
  prevDown  = down;

  if (selected < scroll)             scroll = selected;
  if (selected >= scroll + visible)  scroll = selected - visible + 1;

  if (count == 0) {
    c.setTextColor(THEME_TAB_FG);
    c.setCursor(4, HEIGHT_HEADER);
    c.print("(empty)");
  } else {
    for (int i = 0; i < visible && (scroll + i) < count; i++) {
      int idx = scroll + i;
      bool sel = (idx == selected);
      if (sel) {
        c.fillRoundRect(2, HEIGHT_HEADER + i * LINE_H - 1, LINE_W, LINE_H, 3, THEME_SELECTED_BG);
        c.setTextColor(THEME_SELECTED_FG);
      } else {
        c.setTextColor(THEME_FG);
      }
      c.setCursor(4, HEIGHT_HEADER + i * LINE_H);
      c.print(items[idx].c_str());
    }
    if (scroll > 0) {
      int x = c.width() - 12;
      int y = HEIGHT_HEADER;
      c.fillTriangle(x+3, y, x, y+5, x+6, y+5, THEME_SCROLL);
    }
    if (scroll + visible < count) {
      int x = c.width() - 12;
      int y = HEIGHT_HEADER + (visible - 1) * LINE_H;
      c.fillTriangle(x, y, x+6, y, x+3, y+5, THEME_SCROLL);
    }
  }

  bool fired = enter && !prevEnter;
  prevEnter = enter;
  return fired && count > 0;
}

#include "TabHome.h"
#include "TabMessages.h"
#include "TabPeers.h"

CardputerTaskManager tasks;

std::vector<Tab*> tabs = {
  new TabHome(),
  new TabMessages(),
  new TabPeers(),
};
Counter currentTab(tabs.size(), 0);

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
  if (showTabs) {
    tabs[currentTab]->update();
  } else {
    conv.update();
  }

  // draw tabs
  if (showTabs) {
	  c.setTextSize(1);
	  int tabCount = tabs.size();
	  int tw = c.width() / tabCount;
	  int ty = c.height() - HEIGHT_FOOTER;
	  for (int i = 0; i < tabCount; i++) {
	    int tx = i * tw;
	    bool active = i == currentTab;
	    c.fillRoundRect(tx + 1, ty, tw - 2, HEIGHT_FOOTER - 2, 3, active ? THEME_TAB_ACTIVE_BG : THEME_TAB_BG);
	    c.setTextColor(active ? THEME_TAB_ACTIVE_FG : THEME_TAB_FG);
	    c.drawCenterString(tabs[i]->name, tx + tw / 2, ty + 4);
	  }

	  bool left  = c.isKeyPressed(KEY_LEFT) || c.isKeyPressed(',');
	  bool right = c.isKeyPressed(KEY_RIGHT) || c.isKeyPressed('/');
	  static bool prevLeft  = false;
	  static bool prevRight = false;
	  if (left && !prevLeft)   { --currentTab; tabs[currentTab]->setup(); }
	  if (right && !prevRight) { ++currentTab; tabs[currentTab]->setup(); }
	  prevLeft  = left;
	  prevRight = right;
	}

  // battery indicator
  c.fillRoundRect(c.width() - 24, 4, 20, 10, 3, THEME_BATTERY_BG);
  c.fillRoundRect(c.width() - 23, 5, 18 * (batteryLevel/100.0f), 8, 3, THEME_BATTERY_FG);
  c.setTextColor(THEME_BATTERY_FG);
  c.setTextSize(1);
  c.setCursor(c.width() - 32, 5);
  if (chargingState == 0) {
    c.print("D");
  } else if (chargingState == 1) {
    c.print("C");
  } else {
    c.print("?");
  }
}