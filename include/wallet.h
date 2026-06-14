#pragma once
#include <Arduino.h>

struct WalletInfo {
    bool created;
    String address;
};

void walletInit();
WalletInfo walletGetInfo();
bool walletCreate(const String &pin, String &outAddress, String &outMnemonic, String &error);
bool walletGetMnemonic(const String &pin, String &outMnemonic, String &error);
bool walletUnlockEntropy(const String &pin, uint8_t entropy16[16]);
bool walletVerifyPin(const String &pin);
bool walletSignHash(const String &pin, const String &hashHex, String &outR, String &outS, uint8_t &outRecId, String &error);
void walletErase();
String walletShortAddress(const String &address);
