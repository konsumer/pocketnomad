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

M5Canvas canvas(&M5Cardputer.Display);
microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
SPIClass spi(FSPI);

// this is used to detect 1262, and enable it's radio-hardware
m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);

std::string username = "";
RNS::Identity identity;

// just draw the logo
void draw_logo(int y=canvas.height()/2 - 10) {
	canvas.setTextColor(GREEN);
	canvas.setTextSize(2);
	canvas.drawCenterString("pocketnomad", canvas.width() / 2, y);
	canvas.setTextSize(1);
	canvas.setTextColor(THEME_FG);
}

// get input-text, wait for OK
bool get_input(std::string& var) {
    if (!M5Cardputer.Keyboard.isChange()) return false;
    if (!M5Cardputer.Keyboard.isPressed()) return false;
    auto status = M5Cardputer.Keyboard.keysState();
    
    if (status.enter) {
        return !var.empty(); 
    }
    
    if (status.del) {
        if (!var.empty()) {
            var.pop_back();
        }
    } else {
        for (auto& c : status.word) {
            var += c;
        }
    }
    
    return false;
}

// show full-screen red message, and block
void fatal_error(std::string msg) {
	canvas.fillScreen(THEME_ERROR_BG);
	canvas.setTextColor(THEME_ERROR_FG);
	canvas.drawCenterString(msg.c_str(), canvas.width() / 2, canvas.height() / 2 - 5);
	canvas.pushSprite(0, 0);
	while(1) { delay(1000); }
}

void setup() {
	auto cfg = M5.config();
	M5Cardputer.begin(cfg);
	M5Cardputer.Display.setRotation(1);
	canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());

	canvas.fillScreen(THEME_BG);
	draw_logo();
	canvas.pushSprite(0, 0);

	// Shared SPI bus: SD and LoRa share MISO/MOSI/SCK, each has its own CS
	pinMode(SD_CS,   OUTPUT); digitalWrite(SD_CS,   HIGH);
	pinMode(LORA_CS, OUTPUT); digitalWrite(LORA_CS, HIGH);
	spi.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
	
	if (!SD.begin(SD_CS, spi, SD_SPEED)) {
		fatal_error("SD is required.");
	}

	filesystem.init();
    RNS::Utilities::OS::register_filesystem(filesystem);

	if (ioe.begin()) {
		ioe.setDirection(0, true);
		ioe.setHighImpedance(0, false);
		ioe.digitalWrite(0, true);
	} else {
		fatal_error("LORA error.");
	}

	if (!RNS::Utilities::OS::directory_exists("/pocketnomad")) RNS::Utilities::OS::create_directory("/pocketnomad");
	if (!RNS::Utilities::OS::directory_exists("/pocketnomad/peers")) RNS::Utilities::OS::create_directory("/pocketnomad/peers");
	if (!RNS::Utilities::OS::directory_exists("/pocketnomad/messages")) RNS::Utilities::OS::create_directory("/pocketnomad/messages");

	std::string password = "";

	if (RNS::Utilities::OS::file_exists("/pocketnomad/identity")) {
		IdentityBlob blob;
		bool entered = false;
		while (true) {
			canvas.fillScreen(THEME_BG);
			draw_logo(10);
			canvas.drawCenterString("Welcome back!", canvas.width() / 2, 40);
			if (entered) {
				canvas.setTextColor(THEME_ERROR_FG);
				canvas.drawCenterString("Incorrect password or corrupt file.", canvas.width() / 2, canvas.height() - 14);
				canvas.setTextColor(THEME_FG);
			} else {
				canvas.drawCenterString("This will be required to use the device.", canvas.width() / 2, canvas.height() - 14);
				entered = true;
			}
			canvas.pushSprite(0, 0);
			
			while(!get_input(password)) {
				M5Cardputer.update();
				canvas.fillRect(4, 80, canvas.width(), 12, THEME_BG);
				canvas.setCursor(4, 80);
				canvas.printf("PASSWORD: %s", std::string(password.length(), '*').c_str());
				canvas.pushSprite(0, 0);
				delay(10);
			}

			canvas.fillRect(0, canvas.height() - 14, canvas.width(), 15, THEME_BG);
			canvas.drawCenterString("Please wait.", canvas.width() / 2, canvas.height() - 14);
			canvas.pushSprite(0, 0);

			if (password_open("/pocketnomad/identity", password.c_str(), (uint8_t*)&blob, sizeof(blob))) {
				break;
			}
			password = "";
		}
		username = std::string(blob.name);
		identity = RNS::Identity(false);
		identity.load_private_key(RNS::Bytes(blob.key, 64));
	} else {
		username = "Anonymous Peer";
		canvas.fillScreen(THEME_BG);
		draw_logo(10);
		canvas.drawCenterString("We need to setup an account for you.", canvas.width() / 2, 40);
		canvas.drawCenterString("Other users will see this.", canvas.width() / 2, canvas.height() - 14);
		while(!get_input(username)) {
			M5Cardputer.update();
			canvas.fillRect(4, 80, canvas.width(), 12, THEME_BG);
			canvas.setCursor(4, 80);
			canvas.printf("USERNAME: %s", username.c_str());
			canvas.pushSprite(0, 0);
			delay(10);
		}

		canvas.fillScreen(THEME_BG);
		draw_logo(10);
		canvas.drawCenterString("We need to setup an account for you.", canvas.width() / 2, 40);
		canvas.drawCenterString("This will be required to use the device.", canvas.width() / 2, canvas.height() - 14);
		while(!get_input(password)) {
			M5Cardputer.update();
			canvas.fillRect(4, 80, canvas.width(), 12, THEME_BG);
			canvas.setCursor(4, 80);
			canvas.printf("PASSWORD: %s", std::string(password.length(), '*').c_str());
			canvas.pushSprite(0, 0);
			delay(10);
		}

		identity = RNS::Identity();

		IdentityBlob blob;
		strncpy(blob.name, username.c_str(), sizeof(blob.name) - 1);
		blob.name[sizeof(blob.name) - 1] = '\0';
		memcpy(blob.key, identity.get_private_key().data(), 64);

		if (!password_protect("/pocketnomad/identity", password.c_str(), (uint8_t*)&blob, sizeof(blob))) {
			fatal_error("ID save error.");
		}
	}

	// now we have user name/identity

	// this is used to detect 1262, and enable it's radio-hardware
	if (ioe.begin()) {
		ioe.setDirection(0, true);
		ioe.setHighImpedance(0, false);
		ioe.digitalWrite(0, true);
	}

	// TODO: setup reticulum with lora and tcp/udp over wifi. this should follow user config
	
	// TODO: setup FreeRTOS task for periodic ANNOUNCE send
	// TODO: setup FreeRTOS task for logging incoming ANNOUNCE to disk, as peers
	// TODO: setup FreeRTOS task for logging incoming MESSAGEs (to user) to disk

	// temp: just show that all the setup was completed
	canvas.fillScreen(DARKGREEN);
	canvas.setTextColor(GREEN);
	canvas.drawCenterString("OK", canvas.width() / 2, canvas.height() / 2 - 5);
	canvas.pushSprite(0, 0);
}

void loop() {
	// TODO: switch screen-draw functions
	delay(1000);
}