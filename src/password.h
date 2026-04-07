#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SHA256.h>
#include <CTR.h>
#include <AES.h>
#include <RNG.h>

// first byte of every decrypted blob — wrong password will (almost certainly) not match
// this is not really key-security (they have a 1/256 chance of guessing a password that makes this byte correct)
// but provides a quick password-test. The identity keys are still encrypted, and will fail if they didn't have the actual right password
#define IDENTITY_MAGIC 0x4E

// PBKDF2 iteration count — higher = slower brute-force, slower unlock (~500ms on ESP32 at 10000)
#define PBKDF2_ITERATIONS 10000

// file layout: salt(16) + IV(16) + ciphertext(len)

// encrypt data and write to path; returns true on success
bool password_protect(const char* path, const String& password, const uint8_t* data, size_t len);

// read and decrypt a file written by password_protect
// returns true on success; false means file missing, wrong size, or wrong password
bool password_open(const char* path, const String& password, uint8_t* data, size_t len);
