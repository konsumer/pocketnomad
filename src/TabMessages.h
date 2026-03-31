class TabMessages : public Tab {
public:
  TabMessages() : Tab("Messages") {}

  void setup() override {
    _selected = 0;
    _scroll   = 0;

    // TODO: setup a task that collects _convs
  }

  void update() override {
    c.clear(THEME_BG);
    c.setTextSize(1);

    if (showTabs) {
      std::vector<std::string> items;
      for (int idx : _convs) {
        if (idx >= 0 && idx < (int)nomad.peers.size()) {
          char hex[33];
          const auto& peer = nomad.peers[idx];
          for (int b = 0; b < 16; b++) snprintf(hex + b*2, 3, "%02x", peer[b]);
          items.push_back(hex);
        }
      }

      if (drawList(items, _selected, _scroll)) {
        conv.open(_convs[_selected]);
        showTabs = false;
      }
    }
  }

private:
  std::vector<int> _convs;
  int _selected = 0;
  int _scroll   = 0;
};
