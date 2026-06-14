#include "wallet.h"
#include "storage.h"
#include "crypto_utils.h"
#include "atecc.h"
#include "config.h"

#include <Bitcoin.h>
#include <Conversion.h>
#include <esp_system.h>

static String cachedAddress = "";

static void makeEncryptionKey(const String &pin, uint8_t key32[32]) {
    String serial = ateccSerialHex();
    if (serial == "UNAVAILABLE") serial = "ATECC_PRESENT_NO_SN";

    String material = "HWALLET|" + serial + "|PIN|" + pin;
    sha256Bytes((const uint8_t *)material.c_str(), material.length(), key32);
}

static bool entropyLooksWeak(const uint8_t *buf, size_t len) {
    if (len == 0) return true;

    bool all00 = true;
    bool allFF = true;
    bool allSame = true;

    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0x00) all00 = false;
        if (buf[i] != 0xFF) allFF = false;
        if (buf[i] != buf[0]) allSame = false;
    }

    return all00 || allFF || allSame;
}

static void appendU32(uint8_t *mix, size_t &pos, uint32_t value) {
    mix[pos++] = (uint8_t)(value & 0xFF);
    mix[pos++] = (uint8_t)((value >> 8) & 0xFF);
    mix[pos++] = (uint8_t)((value >> 16) & 0xFF);
    mix[pos++] = (uint8_t)((value >> 24) & 0xFF);
}

static bool getEntropy(uint8_t entropy16[16]) {
    uint8_t mix[128];
    memset(mix, 0, sizeof(mix));
    size_t pos = 0;

    uint8_t ateccRnd[32];
    memset(ateccRnd, 0, sizeof(ateccRnd));
    bool ateccOk = ateccRandom32(ateccRnd) && !entropyLooksWeak(ateccRnd, sizeof(ateccRnd));

    mix[pos++] = ateccOk ? 0xA6 : 0x5A;

    if (ateccOk) {
        memcpy(&mix[pos], ateccRnd, 32);
        pos += 32;
    }

    for (int i = 0; i < 16; i++) {
        appendU32(mix, pos, esp_random());
    }


    appendU32(mix, pos, (uint32_t)micros());
    appendU32(mix, pos, (uint32_t)millis());

    String serial = ateccSerialHex();
    size_t serialLen = serial.length();
    if (serialLen > sizeof(mix) - pos) serialLen = sizeof(mix) - pos;
    memcpy(&mix[pos], serial.c_str(), serialLen);
    pos += serialLen;

    uint8_t digest[32];
    sha256Bytes(mix, pos, digest);

    int attempts = 0;
    while (entropyLooksWeak(digest, 16) && attempts < 5) {
        size_t p2 = 0;
        memset(mix, 0, sizeof(mix));
        for (int i = 0; i < 24; i++) appendU32(mix, p2, esp_random());
        appendU32(mix, p2, (uint32_t)micros());
        appendU32(mix, p2, attempts);
        sha256Bytes(mix, p2, digest);
        attempts++;
    }

    memcpy(entropy16, digest, 16);

    memset(mix, 0, sizeof(mix));
    memset(ateccRnd, 0, sizeof(ateccRnd));
    memset(digest, 0, sizeof(digest));

    return !entropyLooksWeak(entropy16, 16);
}

static String mnemonicFromEntropy16(const uint8_t entropy16[16]) {
    const char *mn = mnemonicFromEntropy(entropy16, 16);
    if (mn == nullptr) return "";
    return String(mn);
}

static bool deriveEthereumAddressFromEntropy(const uint8_t entropy16[16], String &outAddress, String &error) {
    String mnemonic = mnemonicFromEntropy16(entropy16);
    if (mnemonic.length() == 0) {
        error = "Mnemonic fail";
        return false;
    }


    HDPrivateKey root(mnemonic.c_str(), "");
    HDPrivateKey ethKey = root.derive(ETH_DERIVATION_PATH);

    PublicKey pub = ethKey.publicKey();

    uint8_t pub64[64];
    memcpy(pub64, pub.point, 64);

    uint8_t hash[32];
    keccak256Bytes(pub64, 64, hash);

    outAddress = "0x" + hexEncodeLower(&hash[12], 20);

    memset(pub64, 0, sizeof(pub64));
    memset(hash, 0, sizeof(hash));
    return true;
}

void walletInit() {
    StoredWallet sw;
    if (storageLoadWallet(sw)) cachedAddress = sw.address;
}

WalletInfo walletGetInfo() {
    WalletInfo info;
    info.created = storageHasWallet();
    info.address = cachedAddress;

    if (info.created && info.address.length() == 0) {
        StoredWallet sw;
        if (storageLoadWallet(sw)) {
            info.address = sw.address;
            cachedAddress = sw.address;
        }
    }

    return info;
}

bool walletCreate(const String &pin, String &outAddress, String &outMnemonic, String &error) {
    if (storageHasWallet()) {
        error = "Wallet exists";
        return false;
    }

    uint8_t entropy[16];
    uint8_t iv[16];
    uint8_t key[32];
    uint8_t encrypted[16];

    if (!getEntropy(entropy)) {
        error = "Entropy fail";
        return false;
    }

    outMnemonic = mnemonicFromEntropy16(entropy);
    if (outMnemonic.length() == 0) {
        error = "BIP39 fail";
        memset(entropy, 0, sizeof(entropy));
        return false;
    }

    // якшо seed phrase все одно виглядає підозріло, не зберігаємо гаманець.
    if (outMnemonic.startsWith("zoo zoo") || outMnemonic.startsWith("abandon abandon")) {
        error = "Weak entropy";
        memset(entropy, 0, sizeof(entropy));
        return false;
    }

    if (!deriveEthereumAddressFromEntropy(entropy, outAddress, error)) {
        memset(entropy, 0, sizeof(entropy));
        return false;
    }

    randomBytes(iv, 16);
    makeEncryptionKey(pin, key);

    if (!aes256CbcEncrypt16(key, iv, entropy, encrypted)) {
        error = "AES failed";
        memset(entropy, 0, sizeof(entropy));
        memset(key, 0, sizeof(key));
        return false;
    }

    storageSaveWallet(iv, encrypted, outAddress);
    cachedAddress = outAddress;

    memset(entropy, 0, sizeof(entropy));
    memset(key, 0, sizeof(key));

    return true;
}

bool walletUnlockEntropy(const String &pin, uint8_t entropy16[16]) {
    StoredWallet sw;
    if (!storageLoadWallet(sw)) return false;

    uint8_t key[32];
    makeEncryptionKey(pin, key);

    bool ok = aes256CbcDecrypt16(key, sw.iv, sw.encryptedEntropy, entropy16);
    memset(key, 0, sizeof(key));

    if (!ok) return false;

    // Перевіряємо PIN.
    String derived, err;
    if (!deriveEthereumAddressFromEntropy(entropy16, derived, err)) return false;

    derived.toLowerCase();
    String saved = sw.address;
    saved.toLowerCase();

    if (derived != saved) {
        memset(entropy16, 0, 16);
        return false;
    }

    return true;
}

bool walletVerifyPin(const String &pin) {
    uint8_t entropy[16];
    bool ok = walletUnlockEntropy(pin, entropy);
    memset(entropy, 0, sizeof(entropy));
    return ok;
}

bool walletGetMnemonic(const String &pin, String &outMnemonic, String &error) {
    uint8_t entropy[16];
    if (!walletUnlockEntropy(pin, entropy)) {
        error = "Bad PIN";
        return false;
    }

    outMnemonic = mnemonicFromEntropy16(entropy);
    memset(entropy, 0, sizeof(entropy));

    if (outMnemonic.length() == 0) {
        error = "BIP39 fail";
        return false;
    }

    return true;
}


bool walletSignHash(const String &pin, const String &hashHex, String &outR, String &outS, uint8_t &outRecId, String &error) {
    uint8_t hash[32];
    if (!hexDecodeStrict(hashHex, hash, 32)) {
        error = "Bad hash";
        return false;
    }

    uint8_t entropy[16];
    if (!walletUnlockEntropy(pin, entropy)) {
        memset(hash, 0, sizeof(hash));
        error = "Bad PIN";
        return false;
    }

    String mnemonic = mnemonicFromEntropy16(entropy);
    memset(entropy, 0, sizeof(entropy));

    if (mnemonic.length() == 0) {
        memset(hash, 0, sizeof(hash));
        error = "BIP39 fail";
        return false;
    }

    HDPrivateKey root(mnemonic.c_str(), "");
    HDPrivateKey ethKey = root.derive(ETH_DERIVATION_PATH);

    Signature sig = ethKey.sign(hash);
    uint8_t sigBin[65];
    memset(sigBin, 0, sizeof(sigBin));
    sig.bin(sigBin, sizeof(sigBin));

    outR = "0x" + hexEncodeLower(sigBin, 32);
    outS = "0x" + hexEncodeLower(sigBin + 32, 32);
    outRecId = sigBin[64] & 0x01;

    memset(sigBin, 0, sizeof(sigBin));
    memset(hash, 0, sizeof(hash));

    return true;
}

void walletErase() {
    storageEraseWallet();
    cachedAddress = "";
}

String walletShortAddress(const String &address) {
    if (address.length() <= 16) return address;
    return address.substring(0, 8) + ".." + address.substring(address.length() - 6);
}
