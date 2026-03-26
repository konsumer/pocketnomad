class TabHome : public Tab {
public:
  TabHome() : Tab("Home") {}

  void setup() override {}

  void update() override {
    c.clear(THEME_BG);
    
    c.setTextColor(THEME_FG);
    c.setTextSize(1.0);
    c.setCursor(4, 4);
    c.printf("Peers: %d", nomad.getPeerCount());
  }
};