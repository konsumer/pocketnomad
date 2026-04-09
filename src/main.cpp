#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SD.h>
#include <SPI.h>

#include <M5Cardputer.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#include <password.h>

struct IdentityBlob {
	char name[32];    // null-terminated username
	uint8_t key[64];  // RNS private key bytes (prv + sig_prv)
};

// from microReticulum
#include <Reticulum.h>
#include <Identity.h>
#include <Destination.h>
#include <Packet.h>
#include <Transport.h>
#include <Interface.h>
#include <Log.h>
#include <Bytes.h>
#include <Type.h>
#include <Utilities/OS.h>

#include <microStore/FileSystem.h>
#include <microStore/Adapters/UniversalFileSystem.h>

#include "theme.h"
#include "ui.h"

using namespace RNS::Utilities;

// 40MHz high-speed: lower if there are issues
#define SD_SPEED 40000000

// CardputerADV SPI bus (SD card + LoRa share this bus)
#define SPI_SCK  40
#define SPI_MOSI 14
#define SPI_MISO 39
#define SD_CS    12

// CardputerADV built-in SX1262 LoRa control pins
#define LORA_CS    5
#define LORA_IRQ   4
#define LORA_RESET 3
#define LORA_BUSY  6

M5Canvas canvas(&M5Cardputer.Display);
microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
SPIClass spi(FSPI);

m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);

std::string username = "";
RNS::Identity identity;

static void showBoot(const char* subtitle) {
	canvas.fillScreen(THEME_BG);
	canvas.setTextColor(GREEN);
	canvas.setTextSize(2);
	canvas.drawCenterString("pocketnomad", canvas.width() / 2, 10);
	canvas.setTextSize(1);
	canvas.setTextColor(THEME_FG);
	canvas.drawCenterString(subtitle, canvas.width() / 2, 40);
	canvas.pushSprite(0, 0);
}

static void loraEnable() {
	if (ioe.begin()) {
		ioe.setDirection(0, true);
		ioe.setHighImpedance(0, false);
		ioe.digitalWrite(0, true);
	} else {
		UiFatalError("LORA error.");
	}
}

static void action_delete_user() {
	if (UiConfirm("Delete your identity?", "This will completely remove your identity and messages from this device.")) {
		UiMsgBox("Deleted!", "Now, I will reboot, and you can setup a new identity.");
	}
}

void setup() {
	auto cfg = M5.config();
	M5Cardputer.begin(cfg);
	M5Cardputer.Display.setRotation(1);
	canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
	UiInit(&canvas);

	showBoot("Starting up...");

	// Shared SPI bus: SD and LoRa share MISO/MOSI/SCK, each has its own CS
	pinMode(SD_CS,   OUTPUT); digitalWrite(SD_CS,   HIGH);
	pinMode(LORA_CS, OUTPUT); digitalWrite(LORA_CS, HIGH);
	spi.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

	if (!SD.begin(SD_CS, spi, SD_SPEED)) UiFatalError("SD is required.");

	filesystem.init();
	OS::register_filesystem(filesystem);

	loraEnable();

	if (!OS::directory_exists("/pocketnomad")) OS::create_directory("/pocketnomad");
	if (!OS::directory_exists("/pocketnomad/peers")) OS::create_directory("/pocketnomad/peers");
	if (!OS::directory_exists("/pocketnomad/messages")) OS::create_directory("/pocketnomad/messages");

	if (OS::file_exists("/pocketnomad/identity")) {
		// --- Login ---
		IdentityBlob blob;
		bool wrongPassword = false;
		while (true) {
			showBoot(wrongPassword ? "Incorrect password." : "Welcome back!");
			std::string password = UiPasswordInput("Password");
			if (password.empty()) continue;  // cancelled — must enter password to proceed
			UiPleaseWait();
			if (password_open("/pocketnomad/identity", password.c_str(), (uint8_t*)&blob, sizeof(blob))) break;
			wrongPassword = true;
		}
		username = std::string(blob.name);
		identity = RNS::Identity(false);
		identity.load_private_key(RNS::Bytes(blob.key, 64));
	} else {
		// --- Registration ---
		showBoot("Let's set up your account.");
		username = UiTextInput("Username (visible to peers)", "Anonymous Peer");
		if (username.empty()) username = "Anonymous Peer";

		std::string password;
		while (password.empty()) {
			showBoot("Choose a password.");
			password = UiPasswordInput("Password");
		}

		identity = RNS::Identity();

		IdentityBlob blob;
		strncpy(blob.name, username.c_str(), sizeof(blob.name) - 1);
		blob.name[sizeof(blob.name) - 1] = '\0';
		memcpy(blob.key, identity.get_private_key().data(), 64);

		UiPleaseWait();
		if (!password_protect("/pocketnomad/identity", password.c_str(), (uint8_t*)&blob, sizeof(blob))) {
			UiFatalError("ID save error.");
		}
	}

	// now we have username / identity

	// TODO: load settings from disk

	// TODO: setup reticulum with lora and/or tcp/udp over wifi.

	// TODO: setup FreeRTOS task for periodic ANNOUNCE send
	// TODO: setup FreeRTOS task for logging incoming ANNOUNCE to disk, as peers
	// TODO: setup FreeRTOS task for logging incoming MESSAGEs (to user) to disk

	// placeholder tabs until the real screens are wired up
	static std::vector<UiTab> tabs = {
	    {"Home",     {}},
	    {"Messages", {}},
	    {"Peers",    {}},
	    {"Settings", {
	        { "Wipe", "Delete yourself", {}, action_delete_user },
	        { "Lora", "Local Radio" },
	        { "Internet", "All over" },
	    }},
	};
	UiTabs(tabs);
}

void loop() {
	// UiTabs never returns, but keep loop() alive just in case
	delay(1000);
}
