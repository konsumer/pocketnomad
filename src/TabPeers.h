class TabPeers : public Tab {
public:
  TabPeers() : Tab("Peers") {}

  void setup() override {
    _selected = 0;
    _scroll   = 0;
  }

  void update() override {
    c.clear(THEME_BG);
    c.setTextSize(1);

    if (showTabs) {
      std::vector<std::string> items;
      for (const auto& peer : nomad.peers) {
        char hex[33];
        for (int b = 0; b < 16; b++) snprintf(hex + b*2, 3, "%02x", peer[b]);
        items.push_back(hex);
      }

      if (drawList(items, _selected, _scroll)) {
        conv.open(_selected);
        showTabs = false;
      }
    }
  }

private:
  int _selected = 0;
  int _scroll   = 0;
};
