#pragma once
#include <Arduino.h>

struct StoredWallet {
    bool valid;
    uint8_t iv[16];
    uint8_t encryptedEntropy[16];
    String address;
};

void storageInit();
bool storageHasWallet();
bool storageSaveWallet(const uint8_t iv16[16], const uint8_t encryptedEntropy16[16], const String &address);
bool storageLoadWallet(StoredWallet &wallet);
void storageEraseWallet();
