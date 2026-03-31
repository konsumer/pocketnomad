// ConvView: full-screen conversation view with a single peer.
// Call open(peerIdx) to load. ESC sets showTabs=true to return.

class ConvView {
public:
  void open(int peerIdx) {
    _peerIdx   = peerIdx;
    _input[0]  = '\0';
    _inputLen  = 0;
    _prevBS    = false;
    _prevEnter = false;
    _prevEsc   = false;
    memset(_prevKeys, 0, sizeof(_prevKeys));
  }

  void update() {
    c.clear(THEME_BG);
    c.setTextSize(1);

    const int LINE_H   = 10;
    const int INPUT_H  = 12;
    const int TOP_PAD  = 2;
    const int HEADER_H = LINE_H + 2;
    const int inputY   = c.height() - INPUT_H - 2;

    // ESC — go back
    bool esc = c.isKeyPressed(KEY_ESC);
    if (esc && !_prevEsc) {
      showTabs = true;
      _prevEsc = esc;
      return;
    }
    _prevEsc = esc;

    // header: peer hash
    if (_peerIdx >= 0 && _peerIdx < (int)nomad.peers.size()) {
      const auto& peer = nomad.peers[_peerIdx];
      char hex[33];
      for (int b = 0; b < 16; b++) snprintf(hex + b*2, 3, "%02x", peer[b]);
      c.fillRect(0, TOP_PAD, c.width(), HEADER_H, THEME_TAB_ACTIVE_BG);
      c.setTextColor(THEME_TAB_ACTIVE_FG);
      c.setCursor(4, TOP_PAD + 2);
      c.print(hex);
    }

    // message area (placeholder — TODO: load from SD)
    c.setTextColor(THEME_TAB_FG);
    c.setCursor(4, TOP_PAD + HEADER_H + 2);
    c.print("(no messages yet)");

    // input box
    c.drawRoundRect(2, inputY, c.width() - 4, INPUT_H, 3, THEME_FG);
    c.setTextColor(THEME_FG);
    c.setCursor(6, inputY + 2);
    c.print(_input);
    c.fillRect(6 + _inputLen * 6, inputY + 2, 1, LINE_H - 2, THEME_FG);

    // printable ASCII (skip control keys)
    for (int k = 32; k < 127; k++) {
      if (k == KEY_BACKSPACE || k == KEY_ENTER || k == KEY_ESC) continue;
      bool pressed = c.isKeyPressed((char)k);
      if (pressed && !_prevKeys[k] && _inputLen < (int)sizeof(_input) - 1) {
        _input[_inputLen++] = (char)k;
        _input[_inputLen]   = '\0';
      }
      _prevKeys[k] = pressed;
    }

    // backspace
    bool bs = c.isKeyPressed(KEY_BACKSPACE);
    if (bs && !_prevBS && _inputLen > 0) _input[--_inputLen] = '\0';
    _prevBS = bs;

    // enter — send (stub)
    bool enter = c.isKeyPressed(KEY_ENTER);
    if (enter && !_prevEnter && _inputLen > 0) {
      // TODO: send via nomad
      _input[0] = '\0';
      _inputLen = 0;
    }
    _prevEnter = enter;
  }

private:
  int  _peerIdx  = -1;
  char _input[128] = {};
  int  _inputLen = 0;
  bool _prevBS    = false;
  bool _prevEnter = false;
  bool _prevEsc   = false;
  bool _prevKeys[128] = {};
};
