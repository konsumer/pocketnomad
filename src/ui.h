#pragma once

#include <M5Cardputer.h>
#include <functional>
#include <string>
#include <vector>

// Pixel dimensions (landscape, rotation=1)
#define UI_LINE_H    13
#define UI_SCREEN_W  240
#define UI_SCREEN_H  135
#define UI_TABS_H    UI_LINE_H
#define UI_CONTENT_H (UI_SCREEN_H - UI_TABS_H)

// Call once after canvas is created
void UiInit(M5Canvas* canvas);

// Full-screen fatal error — never returns
void UiFatalError(const char* msg);

// Shows title + optional body, waits for any key
void UiMsgBox(const char* title, const char* body = nullptr);

// Confirm dialog. Returns true if Enter, false if OPT/back.
bool UiConfirm(const char* question, const char* body = nullptr);

// Blocking text input. Returns entered string, or "" if cancelled (OPT).
// Navigation chars ,./; are treated as regular text while input is active.
std::string UiTextInput(const char* label, const char* initial = "");

// Same but masked with *
std::string UiPasswordInput(const char* label);

// Pick one option from a list. Returns selected index.
// Returns `current` unchanged if cancelled (OPT/back).
int UiPicker(const char* label, std::vector<std::string> options, int current = 0);

// --- List / Tab types ---

struct UiItem {
    std::string label;
    std::string hint;                      // static right-side label
    std::function<std::string()> hintFn;   // if set, called at draw time instead of hint
    std::function<void()> action;          // called on Enter
    std::vector<UiItem> submenu;           // non-empty → push sub-list on Enter
};

struct UiTab {
    std::string label;
    std::vector<UiItem> items;
};

// Main event loop — runs forever.
//
// Navigation (outside text input):
//   ;  = up        .  = down
//   ,  = prev tab  /  = next tab   (only at top of nav stack)
//   Enter = select item / enter submenu
//   OPT / ` / Backspace = back (pops submenu stack; no-op at top level)
void UiTabs(std::vector<UiTab>& tabs, int initial = 0);

void UiPleaseWait();