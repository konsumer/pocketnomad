class TabPeers : public Tab {
public:
  TabPeers() : Tab("Peers") {}

  void setup() override {}

  void update() override {
    c.clear(THEME_BG);
    c.setTextColor(THEME_FG);
    c.setTextSize(4.0);
    c.drawCenterString("Peers", c.width() / 2, c.height() / 2 - 16);
  }
};