class TabHome : public Tab {
public:
  TabHome() : Tab("Home") {}

  void setup() override {}

  void update() override {
    c.clear(THEME_BG);
    
    c.setTextColor(THEME_FG);
    c.setTextSize(1);
    c.setCursor(4, HEIGHT_HEADER);
    c.printf("Peers: %d", nomad.getPeerCount());
  }
};