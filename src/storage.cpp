#include "storage.h"
#include <Preferences.h>

static Preferences prefs;
static const char *NS = "hwallet";

void storageInit() {
    prefs.begin(NS, false);
}

bool storageHasWallet() {
    return prefs.getBool("created", false);
}

bool storageSaveWallet(const uint8_t iv16[16], const uint8_t encryptedEntropy16[16], const String &address) {
    prefs.putBytes("iv", iv16, 16);
    prefs.putBytes("entropy", encryptedEntropy16, 16);
    prefs.putString("address", address);
    prefs.putBool("created", true);
    return true;
}

bool storageLoadWallet(StoredWallet &wallet) {
    wallet.valid = false;
    if (!storageHasWallet()) return false;

    if (prefs.getBytesLength("iv") != 16) return false;
    if (prefs.getBytesLength("entropy") != 16) return false;

    prefs.getBytes("iv", wallet.iv, 16);
    prefs.getBytes("entropy", wallet.encryptedEntropy, 16);
    wallet.address = prefs.getString("address", "");

    wallet.valid = wallet.address.length() > 0;
    return wallet.valid;
}

void storageEraseWallet() {
    prefs.clear();
}
