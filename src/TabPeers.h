class TabPeers : public Tab {
public:
  TabPeers() : Tab("Peers") {}

  void setup() override {
    _scroll = 0;
  }

  void update() override {
    c.clear(THEME_BG);
    c.setTextColor(THEME_FG);
    c.setTextSize(1.0);

    const int LINE_H   = 10;
    const int TAB_BAR  = 16;
    const int TOP_PAD  = 2;
    const int visible  = (c.height() - TAB_BAR - TOP_PAD) / LINE_H;
    const int count    = (int)nomad.peers.size();

    // clamp scroll
    int maxScroll = count - visible;
    if (maxScroll < 0) maxScroll = 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    if (_scroll < 0)         _scroll = 0;

    if (count == 0) {
      c.setCursor(4, TOP_PAD);
      c.print("No peers yet.");
      return;
    }

    for (int i = 0; i < visible && (_scroll + i) < count; i++) {
      const auto& peer = nomad.peers[_scroll + i];
      c.setCursor(4, TOP_PAD + i * LINE_H);
      char hex[33];
      for (int b = 0; b < 16; b++) snprintf(hex + b*2, 3, "%02x", peer[b]);
      c.print(hex);
    }

    // scroll indicators
    if (_scroll > 0) {
      c.setCursor(c.width() - 8, TOP_PAD);
      c.print("^");
    }
    if (_scroll < maxScroll) {
      c.setCursor(c.width() - 8, TOP_PAD + (visible - 1) * LINE_H);
      c.print("v");
    }

    // edge-triggered up/down scroll
    bool up   = c.isKeyPressed(KEY_UP);
    bool down = c.isKeyPressed(KEY_DOWN);
    if (up   && !_prevUp)   _scroll--;
    if (down && !_prevDown) _scroll++;
    _prevUp   = up;
    _prevDown = down;
  }

private:
  int  _scroll   = 0;
  bool _prevUp   = false;
  bool _prevDown = false;
};
