#include "password.h"

// PBKDF2-HMAC-SHA256, single 32-byte block output
// Produces a key that takes PBKDF2_ITERATIONS × HMAC-SHA256 to derive,
// making offline brute-force proportionally expensive.
static void pbkdf2(const String& password, const uint8_t* salt, uint8_t* out) {
	const uint8_t* pw = (const uint8_t*)password.c_str();
	size_t pwlen = password.length();

	// U1 = HMAC(password, salt || 0x00000001)
	SHA256 sha;
	sha.resetHMAC(pw, pwlen);
	sha.update(salt, 16);
	uint8_t block[4] = {0, 0, 0, 1};
	sha.update(block, sizeof(block));
	uint8_t u[32];
	sha.finalizeHMAC(pw, pwlen, u, sizeof(u));
	memcpy(out, u, 32);

	// Ui = HMAC(password, U_{i-1}), XOR into out
	for (uint32_t i = 1; i < PBKDF2_ITERATIONS; i++) {
		sha.resetHMAC(pw, pwlen);
		sha.update(u, sizeof(u));
		sha.finalizeHMAC(pw, pwlen, u, sizeof(u));
		for (int j = 0; j < 32; j++) out[j] ^= u[j];
	}
}

static void _pw_setup(CTR<AES256>& ctr, const String& password, const uint8_t* salt, const uint8_t* iv) {
	uint8_t key[32];
	pbkdf2(password, salt, key);
	ctr.setKey(key, sizeof(key));
	ctr.setIV(iv, 16);
}

// file layout: salt(16) + IV(16) + ciphertext(len)
bool password_protect(const char* path, const String& password, const uint8_t* data, size_t len) {
	uint8_t salt[16], iv[16];
	RNG.rand(salt, sizeof(salt));
	RNG.rand(iv, sizeof(iv));

	CTR<AES256> ctr;
	_pw_setup(ctr, password, salt, iv);
	uint8_t* encrypted = (uint8_t*)malloc(len);
	if (!encrypted) return false;
	ctr.encrypt(encrypted, data, len);

	File f = SD.open(path, FILE_WRITE);
	if (!f) { free(encrypted); return false; }
	f.write(salt, sizeof(salt));
	f.write(iv, sizeof(iv));
	f.write(encrypted, len);
	f.close();
	free(encrypted);
	return true;
}

bool password_open(const char* path, const String& password, uint8_t* data, size_t len) {
	File f = SD.open(path, FILE_READ);
	if (!f) return false;
	if (f.size() != 32 + len) { f.close(); return false; }  // 16 salt + 16 IV

	uint8_t salt[16], iv[16];
	if (f.read(salt, sizeof(salt)) != sizeof(salt)) { f.close(); return false; }
	if (f.read(iv, sizeof(iv)) != sizeof(iv)) { f.close(); return false; }
	uint8_t* encrypted = (uint8_t*)malloc(len);
	if (!encrypted) { f.close(); return false; }
	if (f.read(encrypted, len) != (int)len) { f.close(); free(encrypted); return false; }
	f.close();

	CTR<AES256> ctr;
	_pw_setup(ctr, password, salt, iv);
	ctr.decrypt(data, encrypted, len);
	free(encrypted);
	return data[0] == IDENTITY_MAGIC;
}
