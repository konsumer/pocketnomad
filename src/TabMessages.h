class TabMessages : public Tab {
public:
  TabMessages() : Tab("Messages") {}

  void setup() override {}

  void update() override {
    c.clear(THEME_BG);
    c.setTextColor(THEME_FG);
    c.setTextSize(4.0);
    c.drawCenterString("Messages", c.width() / 2, c.height() / 2 - 16);
  }
};