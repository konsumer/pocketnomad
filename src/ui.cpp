#include "ui.h"
#include "theme.h"

#include <algorithm>

static M5Canvas* _cv = nullptr;

void UiInit(M5Canvas* canvas) {
    _cv = canvas;
}

// ─── Key reading ──────────────────────────────────────────────────────────────

struct UiKey {
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;
    bool ok    = false;
    bool back  = false;  // OPT always; also ` / backspace in nav mode
    bool del   = false;  // raw backspace (for text editing)
    std::vector<char> chars;
};

// textMode=true  → ,./; pass through as chars, back=OPT only, del=backspace
// textMode=false → ,./; are nav, back=OPT|backspace|`
static UiKey readKey(bool textMode) {
    while (true) {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
            delay(10);
            continue;
        }
        auto& s = M5Cardputer.Keyboard.keysState();
        UiKey k;
        k.ok  = s.enter;
        k.del = s.del;

        if (textMode) {
            k.back = s.opt;
        } else {
            k.back = s.opt || s.del;
        }

        for (char c : s.word) {
            if (!textMode) {
                if (c == ';') { k.up    = true; continue; }
                if (c == '.') { k.down  = true; continue; }
                if (c == ',') { k.left  = true; continue; }
                if (c == '/') { k.right = true; continue; }
                if (c == '`') { k.back  = true; continue; }
            }
            if ((uint8_t)c >= 0x20) k.chars.push_back(c);
        }

        // return if something meaningful happened
        if (k.ok || k.back || k.del || k.up || k.down || k.left || k.right || !k.chars.empty()) {
            return k;
        }
    }
}

// ─── Drawing helpers ──────────────────────────────────────────────────────────

enum class UiTextAlign { Left, Center };

static std::vector<std::string> wrapTextLines(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    if (_cv == nullptr || maxWidth <= 0) {
        lines.push_back(text);
        return lines;
    }

    std::string current;
    auto flush = [&](bool forceEmpty) {
        if (!current.empty() || forceEmpty) {
            lines.push_back(current);
            current.clear();
        }
    };

    size_t i = 0;
    while (i < text.size()) {
        char ch = text[i];
        if (ch == '\n') {
            flush(true);
            ++i;
            continue;
        }

        size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
            ++i;
        }

        std::string word = text.substr(start, i - start);
        std::string candidate = current.empty() ? word : current + " " + word;

        if (_cv->textWidth(candidate.c_str()) <= maxWidth || current.empty()) {
            current = std::move(candidate);
        } else {
            flush(false);
            current = word;
        }

        if (i < text.size() && text[i] == ' ') ++i;
    }

    flush(false);
    if (lines.empty()) lines.push_back(current);
    return lines;
}

static int drawWrappedLines(const std::vector<std::string>& lines, int x, int y, UiTextAlign align) {
    int drawn = 0;
    for (const auto& line : lines) {
        if (align == UiTextAlign::Center) {
            _cv->drawCenterString(line.c_str(), x, y);
        } else {
            _cv->drawString(line.c_str(), x, y);
        }
        y += UI_LINE_H;
        drawn += UI_LINE_H;
    }
    return drawn;
}

static void drawTabBar(const std::vector<UiTab>& tabs, int selected) {
    int n = (int)tabs.size();
    if (n == 0) return;
    int y = UI_SCREEN_H - UI_TABS_H;
    int w = UI_SCREEN_W / n;
    _cv->setTextSize(1);
    _cv->fillRect(0, y, UI_SCREEN_W, UI_TABS_H, THEME_MENU_BG);
    for (int i = 0; i < n; i++) {
        int x = i * w;
        if (i == selected) {
            _cv->fillRect(x, y, w, UI_TABS_H, THEME_MENU_ACTIVE_BG);
            _cv->setTextColor(THEME_MENU_ACTIVE_FG);
        } else {
            _cv->setTextColor(THEME_MENU_FG);
        }
        _cv->drawCenterString(tabs[i].label.c_str(), x + w / 2, y + 3);
    }
}

static void drawList(const std::vector<UiItem>& items, int scroll, int selected) {
    _cv->setTextSize(1);
    _cv->fillRect(0, 0, UI_SCREEN_W, UI_CONTENT_H, THEME_BG);

    int visible = UI_CONTENT_H / UI_LINE_H;

    if (items.empty()) {
        _cv->setTextColor(THEME_HINT);
        _cv->drawCenterString("(empty)", UI_SCREEN_W / 2, UI_CONTENT_H / 2 - 4);
        return;
    }

    for (int i = 0; i < visible && (i + scroll) < (int)items.size(); i++) {
        int idx    = i + scroll;
        int y      = i * UI_LINE_H;
        bool active = (idx == selected);

        _cv->fillRect(0, y, UI_SCREEN_W, UI_LINE_H, active ? THEME_MENU_ACTIVE_BG : THEME_BG);
        _cv->setTextColor(active ? THEME_MENU_ACTIVE_FG : THEME_FG);
        _cv->drawString(items[idx].label.c_str(), 4, y + 3);

        // right-side indicator: submenu arrow takes priority over hint/hintFn
        if (!items[idx].submenu.empty()) {
            _cv->setTextColor(active ? THEME_MENU_ACTIVE_FG : THEME_HINT);
            _cv->drawRightString(">", UI_SCREEN_W - 4, y + 3);
        } else {
            std::string h = items[idx].hintFn ? items[idx].hintFn() : items[idx].hint;
            if (!h.empty()) {
                _cv->setTextColor(active ? THEME_MENU_ACTIVE_FG : THEME_HINT);
                _cv->drawRightString(h.c_str(), UI_SCREEN_W - 4, y + 3);
            }
        }
    }

    // scroll indicators
    _cv->setTextColor(THEME_HINT);
    if (scroll > 0) {
        _cv->drawRightString("^", UI_SCREEN_W - 1, 2);
    }
    if (scroll + visible < (int)items.size()) {
        _cv->drawRightString("v", UI_SCREEN_W - 1, UI_CONTENT_H - UI_LINE_H + 2);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void UiFatalError(const char* msg) {
    _cv->fillScreen(THEME_ERROR_BG);
    _cv->setTextSize(1);
    _cv->setTextColor(THEME_ERROR_FG);
    auto lines      = wrapTextLines(msg ? msg : "", UI_SCREEN_W - 12);
    int blockHeight = (int)lines.size() * UI_LINE_H;
    int startY      = (UI_SCREEN_H - blockHeight) / 2;
    drawWrappedLines(lines, UI_SCREEN_W / 2, startY, UiTextAlign::Center);
    _cv->pushSprite(0, 0);
    while (true) delay(1000);
}

void UiMsgBox(const char* title, const char* body) {
    _cv->fillScreen(THEME_BG);
    _cv->setTextSize(1);
    auto titleLines = wrapTextLines(title ? title : "", UI_SCREEN_W - 12);
    int titleHeight = (int)titleLines.size() * UI_LINE_H;
    int y           = body ? 30 : (UI_SCREEN_H - titleHeight) / 2;
    _cv->setTextColor(THEME_TITLE);
    drawWrappedLines(titleLines, UI_SCREEN_W / 2, y, UiTextAlign::Center);
    if (body) {
        auto bodyLines = wrapTextLines(body, UI_SCREEN_W - 12);
        _cv->setTextColor(THEME_FG);
        drawWrappedLines(bodyLines, UI_SCREEN_W / 2, y + titleHeight + 10, UiTextAlign::Center);
    }
    _cv->pushSprite(0, 0);
    // wait for any key
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) return;
        delay(10);
    }
}

// shared implementation for text/password input
static std::string doTextInput(const char* label, std::string val, bool mask) {
    auto redraw = [&]() {
        _cv->fillScreen(THEME_BG);
        _cv->setTextSize(1);
        _cv->setTextColor(THEME_TITLE);
        auto labelLines = wrapTextLines(label ? label : "", UI_SCREEN_W - 12);
        int labelHeight = drawWrappedLines(labelLines, UI_SCREEN_W / 2, 20, UiTextAlign::Center);
        _cv->setTextColor(THEME_FG);
        std::string shown = (mask ? std::string(val.size(), '*') : val) + '_';
        int inputY       = std::max(55, 20 + labelHeight + 10);
        _cv->drawString(shown.c_str(), 4, inputY);
        _cv->setTextColor(THEME_HINT);
        auto hintLines = wrapTextLines("OPT=cancel  ENTER=ok", UI_SCREEN_W - 12);
        int hintHeight = (int)hintLines.size() * UI_LINE_H;
        int hintY      = UI_SCREEN_H - hintHeight - 8;
        drawWrappedLines(hintLines, UI_SCREEN_W / 2, hintY, UiTextAlign::Center);
        _cv->pushSprite(0, 0);
    };
    redraw();
    while (true) {
        UiKey k = readKey(true);
        if (k.ok && !val.empty()) return val;
        if (k.back) return "";
        if (k.del && !val.empty()) { val.pop_back(); redraw(); continue; }
        for (char c : k.chars) { val += c; redraw(); }
    }
}

std::string UiTextInput(const char* label, const char* initial) {
    return doTextInput(label, initial ? initial : "", false);
}

std::string UiPasswordInput(const char* label) {
    return doTextInput(label, "", true);
}

int UiPicker(const char* label, std::vector<std::string> options, int current) {
    int sel = (current >= 0 && current < (int)options.size()) ? current : 0;
    const int listY = UI_LINE_H + 4;
    const int listH = UI_SCREEN_H - listY - UI_LINE_H;
    const int vis   = listH / UI_LINE_H;
    int scroll = std::max(0, sel - vis / 2);

    auto redraw = [&]() {
        _cv->fillScreen(THEME_BG);
        _cv->setTextSize(1);
        _cv->setTextColor(THEME_TITLE);
        _cv->drawCenterString(label, UI_SCREEN_W / 2, 2);
        _cv->drawFastHLine(0, UI_LINE_H + 1, UI_SCREEN_W, THEME_HINT);
        for (int i = 0; i < vis && (i + scroll) < (int)options.size(); i++) {
            int idx    = i + scroll;
            int y      = listY + i * UI_LINE_H;
            bool active = (idx == sel);
            _cv->fillRect(0, y, UI_SCREEN_W, UI_LINE_H, active ? THEME_MENU_ACTIVE_BG : THEME_BG);
            _cv->setTextColor(active ? THEME_MENU_ACTIVE_FG : THEME_FG);
            if (idx == current) _cv->drawString("*", 2, y + 3);
            _cv->drawString(options[idx].c_str(), 14, y + 3);
        }
        _cv->setTextColor(THEME_HINT);
        if (scroll > 0)
            _cv->drawRightString("^", UI_SCREEN_W - 1, listY + 2);
        if (scroll + vis < (int)options.size())
            _cv->drawRightString("v", UI_SCREEN_W - 1, listY + (vis - 1) * UI_LINE_H + 2);
        _cv->drawCenterString("OPT=cancel  ENTER=ok", UI_SCREEN_W / 2, UI_SCREEN_H - UI_LINE_H + 3);
        _cv->pushSprite(0, 0);
    };
    redraw();

    while (true) {
        UiKey k = readKey(false);
        if (k.up && sel > 0) {
            sel--;
            if (sel < scroll) scroll = sel;
            redraw();
        } else if (k.down && sel < (int)options.size() - 1) {
            sel++;
            if (sel >= scroll + vis) scroll = sel - vis + 1;
            redraw();
        } else if (k.ok) {
            return sel;
        } else if (k.back) {
            return current;
        }
    }
}

// ─── Tab navigator ────────────────────────────────────────────────────────────

struct NavFrame {
    const std::vector<UiItem>* items;
    int scroll   = 0;
    int selected = 0;
    NavFrame(const std::vector<UiItem>* i, int sc = 0, int sel = 0)
        : items(i), scroll(sc), selected(sel) {}
};

void UiTabs(std::vector<UiTab>& tabs, int initial) {
    if (tabs.empty()) return;
    int cur = initial;

    std::vector<NavFrame> stack = {{&tabs[cur].items, 0, 0}};

    auto redraw = [&]() {
        auto& f = stack.back();
        drawList(*f.items, f.scroll, f.selected);
        drawTabBar(tabs, cur);
        _cv->pushSprite(0, 0);
    };
    redraw();

    while (true) {
        UiKey k = readKey(false);
        auto& f = stack.back();
        int n   = (int)f.items->size();
        int vis = UI_CONTENT_H / UI_LINE_H;

        if (k.up) {
            if (f.selected > 0) {
                f.selected--;
                if (f.selected < f.scroll) f.scroll = f.selected;
                redraw();
            }
        } else if (k.down) {
            if (f.selected < n - 1) {
                f.selected++;
                if (f.selected >= f.scroll + vis) f.scroll = f.selected - vis + 1;
                redraw();
            }
        } else if (k.ok && n > 0) {
            if (!(*f.items)[f.selected].submenu.empty()) {
                stack.push_back(NavFrame(&(*f.items)[f.selected].submenu, 0, 0));
                redraw();
            } else if ((*f.items)[f.selected].action) {
                // Copy action before calling: the action may rebuild f.items,
                // which would destroy the original std::function while it executes.
                auto action = (*f.items)[f.selected].action;
                action();
                // Clamp selection in case the rebuild shortened the list
                int newN = (int)f.items->size();
                if (f.selected >= newN) f.selected = std::max(0, newN - 1);
                if (f.scroll > f.selected) f.scroll = f.selected;
                redraw();
            }
        } else if (k.back) {
            if (stack.size() > 1) {
                stack.pop_back();
                redraw();
            }
            // at top level, back is silently ignored
        } else if (stack.size() == 1) {
            // tab switching only when not in a sub-list
            if (k.left && cur > 0) {
                cur--;
                stack = {{&tabs[cur].items, 0, 0}};
                redraw();
            } else if (k.right && cur < (int)tabs.size() - 1) {
                cur++;
                stack = {{&tabs[cur].items, 0, 0}};
                redraw();
            }
        }
    }
}

bool UiConfirm(const char* question, const char* body) {
    std::vector<UiTab> tabs = {
        {"No",     {}},
        {"Yes", {}}
    };
    bool confirmed = false;

    auto redraw = [&]() {
        _cv->fillScreen(THEME_BG);
        _cv->setTextSize(1);
        auto questionLines = wrapTextLines(question ? question : "", UI_SCREEN_W - 12);
        int questionHeight = (int)questionLines.size() * UI_LINE_H;
        int contentHeight  = UI_SCREEN_H - UI_TABS_H;
        int y              = body ? 30 : (contentHeight - questionHeight) / 2;
        _cv->setTextColor(THEME_TITLE);
        drawWrappedLines(questionLines, UI_SCREEN_W / 2, y, UiTextAlign::Center);
        if (body) {
            auto bodyLines = wrapTextLines(body, UI_SCREEN_W - 12);
            _cv->setTextColor(THEME_FG);
            drawWrappedLines(bodyLines, UI_SCREEN_W / 2, y + questionHeight + 10, UiTextAlign::Center);
        }
        drawTabBar(tabs, confirmed ? 1 : 0);
         _cv->pushSprite(0, 0);
    };
    redraw();

    while (true) {
        UiKey k = readKey(false);
        if (k.ok) return confirmed;
        if (k.back) return false;
        if (k.left || k.right) {
            confirmed = confirmed ? false : true;
            redraw();
        }
        delay(10);
    }
}


void UiPleaseWait() {
    _cv->fillScreen(THEME_BG);
    _cv->setTextColor(THEME_HINT);
    _cv->setTextSize(1);
    auto lines      = wrapTextLines("Please wait...", _cv->width() - 12);
    int blockHeight = (int)lines.size() * UI_LINE_H;
    int startY      = (_cv->height() - blockHeight) / 2;
    drawWrappedLines(lines, _cv->width() / 2, startY, UiTextAlign::Center);
    _cv->pushSprite(0, 0);
}
