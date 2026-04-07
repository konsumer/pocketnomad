#include <M5Cardputer.h>
#include "utility/PI4IOE5V6408_Class.hpp"
#include "LoRaRadio.h"
#include <Reticulum.h>
#include <SPI.h>
#include <SD.h>
#include "password.h"

// 40MHz high-speed: lower if there are issues
#define SD_SPEED 40000000

// CardputerADV SPI bus (SD card + LoRa share this bus)
// From M5Unified _pin_table_spi_sd for board_M5CardputerADV
#define SPI_SCK  40
#define SPI_MOSI 14
#define SPI_MISO 39
#define SD_CS    12

// CardputerADV built-in SX1262 LoRa control pins
#define LORA_CS    5
#define LORA_IRQ   4
#define LORA_RESET 3
#define LORA_BUSY  6

// Shared SPI bus for SD + LoRa
SPIClass spi(FSPI);

// ePaper has a max of 8, but use true-color if you got it
#define SCREEN_BPP 24

#define THEME_BG BLACK
#define THEME_FG WHITE
#define THEME_ERROR RED
#define THEME_MENU_BG BLUE
#define THEME_MENU_FG YELLOW
#define THEME_MENU_SELECTED_BG YELLOW
#define THEME_MENU_SELECTED_FG BLACK
#define THEME_LIST_SELECTED_BG WHITE
#define THEME_LIST_SELECTED_FG BLACK
#define THEME_LIST_BG BLACK
#define THEME_LIST_FG WHITE

// OK sends other chars, so this is a special one
#define KEY_OK_BUTTON 254

// tracks current mode device is running in
enum OpState {
	STATE_HOME, // on home screen (summary)
	STATE_MESSAGES, // list messages
	STATE_PEERS, // list peers
	STATE_CONFIG, // configuration screen
	STATE_CHAT, // show message chat-screen
};

OpState currentState = STATE_HOME;

std::vector<std::string> menu_main_items = {
	"Home",
	"Msgs",
	"Peers",
	"Config"
};

std::vector<std::string> menu_config_items = {
	"Wipe Data",
};

M5Canvas canvas(&M5Cardputer.Display);

// this is used to detect 1262, and enable it's radio-hardware
m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);

LoRaRadio* radio = nullptr;
Reticulum reticulum;

Reticulum::Identity identityKeys;
char username[32] = {};
uint8_t destHash[16];
char destHashString[HASH_SIZE * 2 + 1];

void printf_log(const char* fmt, ...);
void println_log(const char* str);
void fatal_error(const char* message);

// ── message / peer storage ───────────────────────────────────────────────────
// Message log  /pocketnomad/messages/HASH  (append-only):
//   each record: [uint16_t clen][uint8_t iv[16]][uint8_t ctext[clen]]
//   plaintext:   [uint32_t ts][uint8_t flags][char text...'\0']
//   flags bit 0: 1=outbound, 0=inbound
//
// Peer file  /pocketnomad/peers/HASH:
//   raw sizeof(PeerData) bytes (plaintext — public info from LoRa announces)
//   Stores all Announce fields needed to reconstruct it for buildData().
//
// Read counter  /pocketnomad/read/HASH:
//   [uint32_t num_read]  (plaintext – not sensitive)
//
// Message count  /pocketnomad/count/HASH:
//   [uint32_t total]  incremented on every msg_append; avoids scanning the
//   message log just to get a count for the unread badge in the list view.

#define LIST_PAGE  8   // rows per page in message/peer lists
#define CHAT_PAGE  15  // messages loaded per chat page (display clips at screen edge)

// mirrors the fields of Reticulum::Announce that we need to persist
struct PeerData {
    char    name[32];
    uint8_t encryptPub[ENCRYPT_KEY_SIZE];
    uint8_t signPub[SIGN_KEY_SIZE];
    uint8_t nameHash[10];
    uint8_t randomHash[10];
    uint8_t ratchetPub[ENCRYPT_KEY_SIZE];
    uint8_t signature[SIGNATURE_SIZE];
    uint8_t destHash[HASH_SIZE];
};

struct ListEntry {
    char     hash[HASH_SIZE * 2 + 1];
    char     name[33];
    uint32_t unread;
};

struct MsgRecord {
    uint32_t timestamp;
    bool     outbound;
    String   text;
};

// AES-256-CTR key derived from identity bytes, cached after first use
static uint8_t g_msgKey[32];
static bool    g_msgKeyReady = false;

// list state (shared between STATE_MESSAGES and STATE_PEERS)
static std::vector<ListEntry> g_listEntries;
static bool    g_listDirty = true;
static int     g_listSel   = 0;
static int     g_listPage  = 0;
static OpState g_listFor   = STATE_HOME;

// chat state
static char                   g_chatHash[HASH_SIZE * 2 + 1] = {};
static char                   g_chatName[33] = {};
static OpState                g_chatReturnState = STATE_MESSAGES;
static String                 g_chatInput;
static std::vector<uint32_t>  g_chatOffsets;    // file offset of each record
static std::vector<MsgRecord> g_chatPage;        // current decrypted page
static int                    g_chatPageStart = 0;
static bool                   g_chatDirty     = true;

void drawLogo(int y = 0) {
	canvas.setTextSize(3);
	canvas.setTextColor(GREEN);
	canvas.drawCenterString("pocketnomad", canvas.width() / 2, y + 14);
	canvas.setTextSize(1);
	canvas.setTextColor(THEME_FG);
}

// draws a menu bar at the bottom; selected highlights the active item
// returns the pressed key
char drawMenu(std::vector<std::string> labels, int selected = -1) {
	const int y = canvas.height() - 10;
	const int w = canvas.width() / labels.size();

	canvas.fillRect(0, canvas.height() - 12, canvas.width(), 12, THEME_MENU_BG);

	if (selected > -1) {
		canvas.fillRect(selected * w, canvas.height() - 12, w, 12, THEME_MENU_SELECTED_BG);
	}

	for (size_t i = 0; i < labels.size(); ++i) {
		canvas.setTextColor(i == selected ? THEME_MENU_SELECTED_FG : THEME_MENU_FG);
		canvas.drawCenterString(labels[i].c_str(), w * i + w / 2, y);
	}
	canvas.setTextColor(THEME_FG);
	if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return -1;
	auto& status = M5Cardputer.Keyboard.keysState();

	// enter gets confused with other keys for some reason
	if (status.enter) {
		return KEY_OK_BUTTON;
	}

	return status.word[0];
}

// similar to drawMenu, but does not return pressed key
void drawList(std::vector<std::string> labels, int selected) {
	for (size_t i = 0; i < labels.size(); ++i) {
		canvas.setTextColor(i == selected ? THEME_LIST_SELECTED_FG : THEME_LIST_FG);
		canvas.fillRect(0, i * 10, canvas.width(), 10, i == selected ? THEME_LIST_SELECTED_BG : THEME_LIST_BG);
		canvas.drawString(labels[i].c_str(), 4, i * 10);
	}
}

// count lines a string would occupy when word-wrapped in [x, maxX] (no drawing)
static int measureWrapped(const String& text, int x, int maxX) {
    int lineW = maxX - x;
    int lines = 1;
    String word, line;
    for (int i = 0; i <= (int)text.length(); i++) {
        char c = i < (int)text.length() ? text[i] : ' ';
        if (c == ' ' || c == '\n') {
            String test = line.length() ? line + ' ' + word : word;
            if (canvas.textWidth(test.c_str()) > lineW && line.length()) {
                lines++;
                line = word;
            } else {
                line = test;
            }
            word = "";
        } else {
            word += c;
        }
    }
    return lines;
}

// draw a string word-wrapped within [x, maxX], starting at y; returns y after last line
int drawWrapped(const String& text, int x, int y, int maxX) {
	int lineW = maxX - x;
	String word, line;
	for (int i = 0; i <= (int)text.length(); i++) {
		char c = i < (int)text.length() ? text[i] : ' ';
		if (c == ' ' || c == '\n') {
			String test = line.length() ? line + ' ' + word : word;
			if (canvas.textWidth(test.c_str()) > lineW && line.length()) {
				canvas.drawString(line.c_str(), x, y);
				y += canvas.fontHeight();
				line = word;
			} else {
				line = test;
			}
			word = "";
		} else {
			word += c;
		}
	}
	if (line.length()) canvas.drawString(line.c_str(), x, y);
	return y + canvas.fontHeight();
}

// blocking. show a confirmation dialog, return whether or not they chose yes
bool displayConfirm(String message) {
	int selected = 0;
	static std::vector<std::string> labels = {"No", "Yes"};
	const int pad = 6;
	while (true) {
		M5Cardputer.update();
		canvas.clear(THEME_BG);
		drawWrapped(message, pad, canvas.height() / 2 - 20, canvas.width() - pad);
		char k = drawMenu(labels, selected);
		canvas.pushSprite(0, 0);
		if (k == KEY_OK_BUTTON) return selected == 1;
		if (k == 'y') selected = 1;
		if (k == 'n') selected = 0;
		if (k == ',') selected = (selected + 1) % 2;
		if (k == '/') selected = (selected + 1) % 2;
		delay(100);
	}
}

// draws main menu bar at the bottom; selected highlights the active item
// switched currentState and returns the pressed key
char drawMainMenu(bool extraSpace = false) {
	char m = drawMenu(menu_main_items, currentState);
	int s = menu_main_items.size();
	if (m != -1) {
		switch(m) {
			case 'h':
				currentState = STATE_HOME;
				break;
			case 'm':
				currentState = STATE_MESSAGES;
				break;
			case 'p':
				currentState = STATE_PEERS;
				break;
			case 'c':
				currentState = STATE_CONFIG;
				break;
			case ',': // left
				currentState = static_cast<OpState>((static_cast<int>(currentState) - 1 + s) % s);
				break;
			case '/': // right
				currentState = static_cast<OpState>((static_cast<int>(currentState) + 1) % s);
				break;
		}
	}
	return m;
}

// ── crypto helpers ───────────────────────────────────────────────────────────

// Use the identity's X25519 private key directly as the AES-256 storage key.
// It's already 32 bytes of high-entropy key material. Cached after first call.
static void msg_ensure_key() {
    if (g_msgKeyReady) return;
    memcpy(g_msgKey, identityKeys.encryptPrivate, 32);
    g_msgKeyReady = true;
}

// Encrypt `len` bytes in-place using AES-256-CTR with a fresh random IV.
// iv_out receives the 16-byte IV used.
static void aes_encrypt(uint8_t* buf, size_t len, uint8_t iv_out[16]) {
    RNG.rand(iv_out, 16);
    CTR<AES256> ctr;
    ctr.setKey(g_msgKey, 32);
    ctr.setIV(iv_out, 16);
    ctr.encrypt(buf, buf, len);
}

// Decrypt `len` bytes in-place using AES-256-CTR with the given IV.
static void aes_decrypt(uint8_t* buf, size_t len, const uint8_t iv[16]) {
    CTR<AES256> ctr;
    ctr.setKey(g_msgKey, 32);
    ctr.setIV(iv, 16);
    ctr.decrypt(buf, buf, len);
}

// ── peer file helpers ─────────────────────────────────────────────────────────

// Save a PeerData to /pocketnomad/peers/HASH (plaintext — public announce info).
static bool peer_save(const char* hashHex, const PeerData& pd) {
    String path = String("/pocketnomad/peers/") + hashHex;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.write((const uint8_t*)&pd, sizeof(PeerData));
    f.close();
    return true;
}

// Load PeerData from /pocketnomad/peers/HASH. Returns false if missing/corrupt.
static bool peer_load(const char* hashHex, PeerData& pd) {
    String path = String("/pocketnomad/peers/") + hashHex;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    if ((size_t)f.size() != sizeof(PeerData)) { f.close(); return false; }
    bool ok = f.read((uint8_t*)&pd, sizeof(PeerData)) == (int)sizeof(PeerData);
    f.close();
    return ok;
}

// Load a peer file and reconstruct a Reticulum::Announce from it.
// Returns false if the peer file is missing or corrupt.
static bool load_peer(const char* hashHex, Reticulum::Announce& ann) {
    PeerData pd;
    if (!peer_load(hashHex, pd)) return false;
    memset(&ann, 0, sizeof(ann));
    ann.valid = true;
    memcpy(ann.keyPubEncrypt,   pd.encryptPub, ENCRYPT_KEY_SIZE);
    memcpy(ann.keyPubSignature, pd.signPub,    SIGN_KEY_SIZE);
    memcpy(ann.nameHash,        pd.nameHash,   10);
    memcpy(ann.randomHash,      pd.randomHash, 10);
    memcpy(ann.ratchetPub,      pd.ratchetPub, ENCRYPT_KEY_SIZE);
    memcpy(ann.signature,       pd.signature,  SIGNATURE_SIZE);
    memcpy(ann.destinationHash, pd.destHash,   HASH_SIZE);
    return true;
}

// Extract a display name from LXMF announce appData (msgpack string).
// Falls back to empty string on failure.
static void peer_extract_name(const uint8_t* appData, size_t len, char name[32]) {
    memset(name, 0, 32);
    if (!appData || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = appData[i];
        size_t slen = 0, off = 0;
        if ((b & 0xe0) == 0xa0) { slen = b & 0x1f; off = i + 1; }          // fixstr
        else if (b == 0xd9 && i + 1 < len) { slen = appData[i+1]; off = i + 2; } // str8
        if (slen && off + slen <= len) {
            size_t copy = slen < 31 ? slen : 31;
            memcpy(name, appData + off, copy);
            return;
        }
    }
}

// ── message file helpers ──────────────────────────────────────────────────────

// Append an encrypted message record to /pocketnomad/messages/HASH.
static uint32_t msg_read_u32(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f || f.size() < 4) { if (f) f.close(); return 0; }
    uint32_t n = 0;
    f.read((uint8_t*)&n, 4);
    f.close();
    return n;
}

static void msg_write_u32(const char* path, uint32_t n) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    f.write((uint8_t*)&n, 4);
    f.close();
}

static uint32_t msg_get_read(const char* hashHex) {
    return msg_read_u32((String("/pocketnomad/read/") + hashHex).c_str());
}
static void msg_set_read(const char* hashHex, uint32_t n) {
    msg_write_u32((String("/pocketnomad/read/") + hashHex).c_str(), n);
}

static uint32_t msg_get_total(const char* hashHex) {
    return msg_read_u32((String("/pocketnomad/count/") + hashHex).c_str());
}
static void msg_inc_total(const char* hashHex) {
    msg_write_u32((String("/pocketnomad/count/") + hashHex).c_str(),
                  msg_get_total(hashHex) + 1);
}

bool msg_append(const char* hashHex, bool outbound, const String& text) {
    msg_ensure_key();
    size_t textLen   = text.length();
    size_t plainLen  = 5 + textLen;             // ts(4) + flags(1) + text
    uint8_t* buf = (uint8_t*)malloc(plainLen);
    if (!buf) return false;
    uint32_t ts = millis() / 1000;
    memcpy(buf, &ts, 4);
    buf[4] = outbound ? 0x01 : 0x00;
    memcpy(buf + 5, text.c_str(), textLen);
    uint8_t iv[16];
    aes_encrypt(buf, plainLen, iv);
    String path = String("/pocketnomad/messages/") + hashHex;
    File f = SD.open(path.c_str(), FILE_APPEND);
    if (!f) { free(buf); return false; }
    uint16_t clen = (uint16_t)plainLen;
    f.write((uint8_t*)&clen, 2);
    f.write(iv, 16);
    f.write(buf, plainLen);
    f.close();
    free(buf);
    msg_inc_total(hashHex);
    return true;
}

// Scan message file and populate offsets[] with the file position of each record.
static bool msg_load_offsets(const char* hashHex, std::vector<uint32_t>& offsets) {
    offsets.clear();
    String path = String("/pocketnomad/messages/") + hashHex;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    uint32_t pos = 0, sz = f.size();
    while (pos + 18 <= sz) {   // need at least 2(len)+16(iv)
        offsets.push_back(pos);
        uint16_t clen = 0;
        f.seek(pos);
        if (f.read((uint8_t*)&clen, 2) != 2) break;
        pos += 2 + 16 + clen;
    }
    f.close();
    return true;
}

// Decrypt messages [start, start+count) from the offset index into g_chatPage.
static void msg_load_page(const char* hashHex, int start, int count) {
    msg_ensure_key();
    g_chatPage.clear();
    if (g_chatOffsets.empty()) return;
    String path = String("/pocketnomad/messages/") + hashHex;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return;
    int end = start + count;
    if (end > (int)g_chatOffsets.size()) end = g_chatOffsets.size();
    for (int i = start; i < end; i++) {
        f.seek(g_chatOffsets[i]);
        uint16_t clen = 0;
        if (f.read((uint8_t*)&clen, 2) != 2 || clen < 5) continue;
        uint8_t iv[16];
        if (f.read(iv, 16) != 16) continue;
        uint8_t* buf = (uint8_t*)malloc(clen);
        if (!buf) continue;
        if (f.read(buf, clen) != clen) { free(buf); continue; }
        aes_decrypt(buf, clen, iv);
        MsgRecord rec;
        memcpy(&rec.timestamp, buf, 4);
        rec.outbound = buf[4] & 0x01;
        rec.text = String((char*)buf + 5);
        g_chatPage.push_back(rec);
        free(buf);
    }
    f.close();
}

// ── list loading ──────────────────────────────────────────────────────────────

// Scans the appropriate directory and fills g_listEntries.
// For STATE_MESSAGES also computes unread counts.
static void list_load(OpState forState) {
    g_listEntries.clear();
    const char* dir_path = (forState == STATE_MESSAGES)
        ? "/pocketnomad/messages" : "/pocketnomad/peers";
    File dir = SD.open(dir_path);
    if (!dir) return;
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        if (f.isDirectory()) { f.close(); continue; }
        const char* raw = f.name();
        const char* slash = strrchr(raw, '/');
        const char* fname = slash ? slash + 1 : raw;
        f.close();
        ListEntry e = {};
        strncpy(e.hash, fname, sizeof(e.hash) - 1);
        PeerData pd = {};
        if (peer_load(e.hash, pd) && pd.name[0]) {
            memcpy(e.name, pd.name, 32);
        } else {
            strncpy(e.name, e.hash, 8);
        }
        if (forState == STATE_MESSAGES) {
            uint32_t total = msg_get_total(e.hash);
            uint32_t rd    = msg_get_read(e.hash);
            e.unread = total > rd ? total - rd : 0;
        }
        g_listEntries.push_back(e);
    }
    dir.close();
    g_listFor = forState;
}

// ── processes one keyboard event into var ─────────────────────────────────────
// processes one keyboard event into var; returns true when the user presses ENTER with non-empty input
// caller is responsible for display and calling M5Cardputer.update() each iteration
bool wait_input(String* var) {
	if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return false;
	auto& status = M5Cardputer.Keyboard.keysState();
	if (status.enter) return !var->isEmpty();
	if (status.del) {
		if (!var->isEmpty()) var->remove(var->length() - 1);
	} else {
		for (auto& c : status.word) {
			if (var->length() < 31) *var += c;
		}
	}
	return false;
}

// nothing is setup, so ask the user some questions and save identity
void getIdentity() {
	String input_username = "Anonymous Peer";
	String input_password;

	canvas.clear(THEME_BG);
	drawLogo();
	canvas.drawCenterString("We need to setup an id for you.", canvas.width() / 2, 50);
	canvas.drawString("Name:", 4, 75);
	canvas.drawCenterString("Others will see this.", canvas.width() / 2, canvas.height() - 20);
	canvas.drawCenterString("Press ENTER when done.", canvas.width() / 2, canvas.height() - 10);
	while (!wait_input(&input_username)) {
		M5Cardputer.update();
		canvas.fillRect(0, 88, canvas.width(), 16, THEME_BG);
		canvas.drawString("Name:", 4, 75);
		canvas.setCursor(4, 90);
		canvas.printf("> %s", input_username.c_str());
		canvas.pushSprite(0, 0);
	}

	canvas.clear(THEME_BG);
	drawLogo();
	canvas.drawCenterString("We need to setup an id for you.", canvas.width() / 2, 50);
	canvas.drawString("Password:", 4, 75);
	canvas.drawCenterString("Required on reset to load your id.", canvas.width() / 2, canvas.height() - 20);
	canvas.drawCenterString("Press ENTER when done.", canvas.width() / 2, canvas.height() - 10);
	while (!wait_input(&input_password)) {
		M5Cardputer.update();
		canvas.fillRect(0, 88, canvas.width(), 16, THEME_BG);
		canvas.setCursor(4, 90);
		String stars = "";
		for (size_t i = 0; i < input_password.length(); i++) stars += '*';
		canvas.printf("> %s", stars.c_str());
		canvas.pushSprite(0, 0);
	}

	if (!reticulum.identityCreate(identityKeys)) {
		fatal_error("Could not create identity.");
	}

	// pack into blob: magic(1) + identity(128) + username(32)
	uint8_t blob[161] = {};
	blob[0] = IDENTITY_MAGIC;
	reticulum.identityToBytes(identityKeys, blob + 1);
	size_t ulen = input_username.length();
	if (ulen > 31) ulen = 31;
	memcpy(blob + 129, input_username.c_str(), ulen);

	if (!password_protect("/pocketnomad/identity", input_password, blob, sizeof(blob))) {
		fatal_error("Could not save identity.");
	}

	memcpy(username, blob + 129, 32);
}

// the file exists, so ask user for password, until it can be opened
void getPassword() {
	String input_password;
	String status_msg = "";

	canvas.clear(THEME_BG);
	drawLogo();
	canvas.drawCenterString("Password is required to continue.", canvas.width() / 2, 50);
	canvas.drawString("Password:", 4, 75);
	canvas.drawCenterString("Press ENTER when done.", canvas.width() / 2, canvas.height() - 10);
	canvas.pushSprite(0, 0);

	while (true) {
		M5Cardputer.update();
		if (wait_input(&input_password)) {
			uint8_t blob[161];
			if (password_open("/pocketnomad/identity", input_password, blob, sizeof(blob))) {
				reticulum.identityFromBytes(identityKeys, blob + 1);
				memcpy(username, blob + 129, 32);
				break;
			}
			input_password.clear();
			status_msg = "Wrong password, try again.";
		}
		canvas.fillRect(0, 88, canvas.width(), 16, THEME_BG);
		canvas.setCursor(4, 90);
		String stars = "";
		for (size_t i = 0; i < input_password.length(); i++) stars += '*';
		canvas.printf("> %s", stars.c_str());
		if (status_msg.length()) {
			canvas.fillRect(0, 108, canvas.width(), 12, THEME_BG);
			canvas.drawCenterString(status_msg.c_str(), canvas.width() / 2, 108);
		}
		canvas.pushSprite(0, 0);
	}
}

// show main summary screen
void displayHome() {
	canvas.clear(THEME_BG);
	canvas.drawString(username, 4, 4);
	canvas.drawString(destHashString, 4, 14);
	drawMainMenu();
}

// shared list renderer for messages and peers
static void displayList(OpState forState) {
    if (g_listDirty) {
        list_load(forState);
        g_listDirty = false;
    }
    canvas.clear(THEME_BG);
    int total = g_listEntries.size();
    if (total == 0) {
        canvas.drawCenterString(forState == STATE_MESSAGES ? "No messages" : "No peers",
                                canvas.width() / 2, canvas.height() / 2 - 5);
    } else {
        int start = g_listPage * LIST_PAGE;
        int end   = start + LIST_PAGE;
        if (end > total) end = total;
        std::vector<std::string> labels;
        for (int i = start; i < end; i++) {
            String s = g_listEntries[i].name;
            if (forState == STATE_MESSAGES && g_listEntries[i].unread)
                s += " (" + String(g_listEntries[i].unread) + ")";
            labels.push_back(s.c_str());
        }
        drawList(labels, g_listSel - start);
    }
    char key = drawMainMenu();
    if (key == ';' && g_listSel > 0) {
        g_listSel--;
        if (g_listSel < g_listPage * LIST_PAGE) g_listPage--;
    }
    if (key == '.' && g_listSel < total - 1) {
        g_listSel++;
        if (g_listSel >= (g_listPage + 1) * LIST_PAGE) g_listPage++;
    }
    if (key == KEY_OK_BUTTON && total > 0) {
        memcpy(g_chatHash, g_listEntries[g_listSel].hash, HASH_SIZE * 2 + 1);
        g_chatReturnState = forState;
        g_chatDirty  = true;
        currentState = STATE_CHAT;
    }
}

// list messages from disk
void displayMessageList() { displayList(STATE_MESSAGES); }

// list known peers from disk
void displayPeerList() { displayList(STATE_PEERS); }

// show settings screen
void displayConfig () {
	static int listItem = 0;
	canvas.clear(THEME_BG);
	drawList(menu_config_items, listItem);
	char key = drawMainMenu();
	
	if (key == ';') { // up
		listItem = (listItem - 1 + menu_config_items.size()) % menu_config_items.size();
	}
	
	if (key == '.') { // down
		listItem = (listItem + 1) % menu_config_items.size();
	}

	if (key == KEY_OK_BUTTON) {
		if (displayConfirm("Are you sure you want to wipe all of your data? This will delete messages and identity!")) {
			SD.remove("/pocketnomad/identity");
			const char* wipe_dirs[] = {
				"/pocketnomad/messages",
				"/pocketnomad/peers",
				"/pocketnomad/read",
				"/pocketnomad/count",
			};
			for (auto& dpath : wipe_dirs) {
				File dir = SD.open(dpath);
				if (!dir) continue;
				while (true) {
					File f = dir.openNextFile();
					if (!f) break;
					const char* raw = f.name();
					const char* slash = strrchr(raw, '/');
					String path = String(dpath) + "/" + (slash ? slash + 1 : raw);
					f.close();
					SD.remove(path.c_str());
				}
				dir.close();
			}
			esp_restart();
		}
	}
}

void displayChat() {
    if (g_chatDirty) {
        g_chatOffsets.clear();
        g_chatPage.clear();
        g_chatInput = "";
        msg_load_offsets(g_chatHash, g_chatOffsets);
        int total = g_chatOffsets.size();
        g_chatPageStart = total > CHAT_PAGE ? total - CHAT_PAGE : 0;
        msg_load_page(g_chatHash, g_chatPageStart, CHAT_PAGE);
        msg_set_read(g_chatHash, total);
        // load peer name once
        PeerData pd = {};
        memset(g_chatName, 0, sizeof(g_chatName));
        if (peer_load(g_chatHash, pd) && pd.name[0]) {
            memcpy(g_chatName, pd.name, 32);
        } else {
            strncpy(g_chatName, g_chatHash, 8);
        }
        g_chatDirty = false;
    }

    const int nameH  = 10;
    const int inputH = 12;
    const int msgTop = nameH;
    const int msgBot = canvas.height() - inputH;

    canvas.clear(THEME_BG);

    // header: peer name
    canvas.fillRect(0, 0, canvas.width(), nameH, THEME_MENU_BG);
    canvas.setTextColor(THEME_MENU_FG);
    canvas.drawString(g_chatName, 4, 0);
    canvas.setTextColor(THEME_FG);

    // messages — bottom-aligned, clipped to [msgTop, msgBot)
    if (g_chatPage.empty()) {
        canvas.drawCenterString("No messages", canvas.width() / 2, msgTop + (msgBot - msgTop) / 2 - 5);
    } else {
        const int fh        = canvas.fontHeight();
        const int available = msgBot - msgTop;
        int totalH = 0;
        for (auto& m : g_chatPage)
            totalH += measureWrapped((m.outbound ? "> " : "< ") + m.text, 4, canvas.width() - 4) * fh;
        // anchor newest message at the bottom; clip cuts off oldest if overflow
        int y = msgBot - totalH;
        canvas.setClipRect(0, msgTop, canvas.width(), available);
        for (auto& m : g_chatPage) {
            String line = (m.outbound ? "> " : "< ") + m.text;
            y = drawWrapped(line, 4, y, canvas.width() - 4);
        }
        canvas.clearClipRect();
    }

    // input bar
    canvas.fillRect(0, msgBot, canvas.width(), inputH, THEME_MENU_BG);
    canvas.setTextColor(THEME_MENU_FG);
    canvas.drawString((g_chatInput + "_").c_str(), 4, msgBot + 1);
    canvas.setTextColor(THEME_FG);

    // keyboard handling
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.opt) {
        currentState = g_chatReturnState;
        return;
    }
    if (!st.word.empty() && (st.word[0] == ';' || st.word[0] == '.')) {
        // always consume scroll keys — never let them reach the text input
        int total = g_chatOffsets.size();
        if (st.word[0] == ';' && g_chatPageStart > 0) {
            g_chatPageStart--;
            g_chatPage.clear();
            msg_load_page(g_chatHash, g_chatPageStart, CHAT_PAGE);
        }
        if (st.word[0] == '.' && g_chatPageStart + CHAT_PAGE < total) {
            g_chatPageStart++;
            g_chatPage.clear();
            msg_load_page(g_chatHash, g_chatPageStart, CHAT_PAGE);
        }
        return;
    }
    if (st.enter) {
        if (!g_chatInput.isEmpty()) {
            msg_append(g_chatHash, true, g_chatInput);
            g_chatInput = "";
            g_chatOffsets.clear();
            msg_load_offsets(g_chatHash, g_chatOffsets);
            int total = g_chatOffsets.size();
            g_chatPageStart = total > CHAT_PAGE ? total - CHAT_PAGE : 0;
            g_chatPage.clear();
            msg_load_page(g_chatHash, g_chatPageStart, CHAT_PAGE);
            msg_set_read(g_chatHash, total);
            g_listDirty = true;
        }
        return;
    }
    if (st.del) {
        if (!g_chatInput.isEmpty()) g_chatInput.remove(g_chatInput.length() - 1);
        return;
    }
    for (auto c : st.word) {
        if (g_chatInput.length() < 200) g_chatInput += c;
    }
}

void setup() {
	Serial.begin(9600);
	while (!Serial && millis() < 1000); // wait for serial connect or 1s, whichever comes first
	
	M5Cardputer.begin();
	M5Cardputer.Display.setRotation(1);
	canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
	// canvas.setTextSize((float)canvas.width() / 160);

	canvas.setColorDepth(1);
	canvas.setPaletteColor(1, THEME_FG);
	canvas.setTextScroll(true);
	canvas.clear(THEME_BG);

	// Shared SPI bus: SD and LoRa share MISO/MOSI/SCK, each has its own CS
	pinMode(SD_CS,   OUTPUT); digitalWrite(SD_CS,   HIGH);
	pinMode(LORA_CS, OUTPUT); digitalWrite(LORA_CS, HIGH);
	spi.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

	println_log("Initializing SD...");
	if (!SD.begin(SD_CS, spi, SD_SPEED)) {
		fatal_error("SD Card failed!");
	}
	
	LoRaConfig cfg;
	cfg.spi  = &spi;
	cfg.cs   = LORA_CS;
	cfg.irq  = LORA_IRQ;
	cfg.rst  = LORA_RESET;
	cfg.busy = LORA_BUSY;
	radio = new LoRaRadio(cfg);

	radio->setRfSetup([]() {
		if (ioe.begin()) {
			ioe.setDirection(0, true);
			ioe.setHighImpedance(0, false);
			ioe.digitalWrite(0, true);
		}
	});

	radio->onReceive([](const String& data, float rssi, float snr) {
		Reticulum::Packet packet;
		if (!reticulum.decodePacket((const uint8_t*)data.c_str() + 1, data.length() - 1, packet)) return;

		if (packet.packetType == PACKET_ANNOUNCE) {
			Reticulum::Announce announce;
			if (reticulum.announceParsePacket(packet, announce) && announce.valid) {
				char hashHex[HASH_SIZE * 2 + 1];
				for (int i = 0; i < HASH_SIZE; i++) sprintf(hashHex + i * 2, "%02x", announce.destinationHash[i]);

				PeerData pd = {};
				// preserve existing name if already known, otherwise extract from appData
				if (!peer_load(hashHex, pd) || !pd.name[0]) {
					peer_extract_name(announce.appData, announce.appDataLen, pd.name);
				}
				memcpy(pd.encryptPub, announce.keyPubEncrypt,  ENCRYPT_KEY_SIZE);
				memcpy(pd.signPub,    announce.keyPubSignature, SIGN_KEY_SIZE);
				memcpy(pd.nameHash,   announce.nameHash,   10);
				memcpy(pd.randomHash, announce.randomHash, 10);
				memcpy(pd.ratchetPub, announce.ratchetPub, ENCRYPT_KEY_SIZE);
				memcpy(pd.signature,  announce.signature,  SIGNATURE_SIZE);
				memcpy(pd.destHash,   announce.destinationHash, HASH_SIZE);
				peer_save(hashHex, pd);
				g_listDirty = true;

				printf_log("Peer: %s (%s)\n", pd.name[0] ? pd.name : "unknown", hashHex);
				reticulum.freeAnnounce(announce);
			}
		}

		reticulum.freePacket(packet);
	});

	println_log("Initializing LoRa...");
	if (radio->begin()) {
		println_log("Listening...");
	} else {
		fatal_error("Init failed!");
	}

	if (!SD.exists("/pocketnomad")) SD.mkdir("/pocketnomad");
	if (!SD.exists("/pocketnomad/peers")) SD.mkdir("/pocketnomad/peers");
	if (!SD.exists("/pocketnomad/messages")) SD.mkdir("/pocketnomad/messages");
	if (!SD.exists("/pocketnomad/read")) SD.mkdir("/pocketnomad/read");
	if (!SD.exists("/pocketnomad/count")) SD.mkdir("/pocketnomad/count");

	// Use this to test id-flow
	// SD.remove("/pocketnomad/identity");

	canvas.setColorDepth(SCREEN_BPP);
	canvas.setTextScroll(false);
	canvas.setTextColor(THEME_FG);

	if (SD.exists("/pocketnomad/identity")) {
		getPassword();
	} else {
		getIdentity();
	}

  reticulum.getDestinationHash(identityKeys, "lxmf.delivery", destHash);
	for (int i = 0; i < HASH_SIZE; i++) sprintf(destHashString + i * 2, "%02x", destHash[i]);
}

void loop() {
    M5Cardputer.update();
    radio->update();

    static OpState prevState = STATE_HOME;
    if (currentState != prevState) {
        // leaving chat — free decrypted page (keeps offset index; it's tiny)
        if (prevState == STATE_CHAT) {
            g_chatOffsets.clear(); g_chatOffsets.shrink_to_fit();
            g_chatPage.clear();    g_chatPage.shrink_to_fit();
            g_chatInput = "";
            memset(g_chatName, 0, sizeof(g_chatName));
        }
        // entering list views — reset cursor but keep cached entries unless dirty
        if (currentState == STATE_MESSAGES || currentState == STATE_PEERS) {
            if (g_listFor != currentState) g_listDirty = true; // different list type
            g_listSel  = 0;
            g_listPage = 0;
        }
        if (currentState == STATE_CHAT) {
            g_chatDirty = true;
        }
        prevState = currentState;
    }

    switch(currentState) {
        case STATE_HOME:     displayHome();        break;
        case STATE_MESSAGES: displayMessageList(); break;
        case STATE_PEERS:    displayPeerList();    break;
        case STATE_CONFIG:   displayConfig();      break;
        case STATE_CHAT:     displayChat();        break;
    }

    canvas.pushSprite(0, 0);
}

// logging goes to screen (initiially) and serial
// but eventually will be just serial

void printf_log(const char* fmt, ...) {
	char buf[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	Serial.print(buf);
	canvas.printf(buf);
	canvas.pushSprite(0, 0);
}

void println_log(const char* str) {
	printf_log("%s\n", str);
}

// if color-depth is 1, this turns all logging to red, outputs the error, and just spins on it
void fatal_error(const char* message) {
	canvas.setPaletteColor(1, THEME_ERROR);
	println_log(message);
	while (true) delay(1000);
}
