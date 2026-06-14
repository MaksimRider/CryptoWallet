#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "display.h"
#include "encoder.h"
#include "atecc.h"
#include "storage.h"
#include "wallet.h"
#include "protocol.h"

static const String menuItems[] = {
    "Create Wallet",
    "Show Address",
    "Show Balance",
    "Send ETH",
    "Backup Seed",
    "Device Info",
    "Wipe Wallet"
};
static const int menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
static int menuIndex = 0;

static bool txPending = false;
static TxRequest pendingTx;

static bool balancePending = false;
static bool balanceViewActive = false;
static uint32_t balanceRequestStartedMs = 0;

static bool notificationActive = false;
static String notificationLine1 = "";
static String notificationLine2 = "";

static bool sendSessionActive = false;
static uint32_t sendSessionStartedMs = 0;

static void showMenu();
static bool handleMenuClick();
static void handleTxConfirm(EncoderEvent ev);
static void handleBalanceWait(EncoderEvent ev);
static void handleBalanceView(EncoderEvent ev);
static void handleNotification(EncoderEvent ev);
static void handleSendSession(EncoderEvent ev);
static void handleSerial();
static String enterPin(const String &title);
static void showLongTextPages(const String &title, const String &text);
static void showMnemonicWords(const String &mnemonic);
static String getMnemonicWord(const String &mnemonic, int targetIndex);
static int mnemonicWordCount(const String &mnemonic);

void setup() {
    Serial.begin(115200);
    delay(600);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_FREQ);

    displayInit();
    encoderInit();
    storageInit();
    ateccInit();
    walletInit();

    displayMessage("Hardware Wallet", "FW " FW_VERSION);
    delay(900);

    displayMessage("Checking SE", "Please wait");
    String seSerial = ateccSerialHex();
    bool seOk = (seSerial != "UNAVAILABLE");

    displayMessage("LCD:OK", String("ATECC:") + (seOk ? "OK" : "FAIL"));
    delay(1000);

    Serial.println();
    Serial.println("=== Hardware Wallet FW " FW_VERSION " ===");
    Serial.println("PlatformIO + ESP32 + uBitcoin");
    Serial.print("ATECC608: ");
    Serial.println(seOk ? "OK" : "FAIL");
    Serial.print("ATECC Serial: ");
    Serial.println(seSerial);
    Serial.println("ETH derivation path: " ETH_DERIVATION_PATH);
    Serial.println("Protocol:");
    Serial.println("PC -> ESP32: {\"cmd\":\"address\"}");
    Serial.println("ESP32 -> PC: {\"cmd\":\"balance\",\"address\":\"0x...\"}");
    Serial.println("ESP32 -> PC: {\"cmd\":\"open_send\",\"address\":\"0x...\"}");
    Serial.println();

    showMenu();
}

void loop() {
    handleSerial();

    EncoderEvent ev = encoderUpdate();

    if (txPending) {
        if (ev != ENC_NONE) handleTxConfirm(ev);
        return;
    }

    if (balancePending) {
        handleBalanceWait(ev);
        return;
    }

    if (balanceViewActive) {
        handleBalanceView(ev);
        return;
    }

    if (notificationActive) {
        handleNotification(ev);
        return;
    }

    if (sendSessionActive) {
        handleSendSession(ev);
        return;
    }

    if (ev == ENC_NONE) return;

    if (ev == ENC_RIGHT) {
        menuIndex++;
        if (menuIndex >= menuCount) menuIndex = 0;
        showMenu();
    } else if (ev == ENC_LEFT) {
        menuIndex--;
        if (menuIndex < 0) menuIndex = menuCount - 1;
        showMenu();
    } else if (ev == ENC_CLICK) {
        bool shouldReturnToMenu = handleMenuClick();
        if (shouldReturnToMenu && !balancePending && !txPending && !sendSessionActive) showMenu();
    }
}

static void showMenu() {
    displayMenu("Wallet Menu", menuItems[menuIndex]);
}

static bool handleMenuClick() {
    if (menuIndex == 0) { // Create Wallet
        if (walletGetInfo().created) {
            displayMessage("Wallet exists", "Use wipe first");
            delay(1500);
            return true;
        }

        String pin = enterPin("Create PIN");
        String address, mnemonic, error;

        displayMessage("Creating", "BIP39/ETH...");
        Serial.println("Creating wallet. This may take some seconds...");

        if (walletCreate(pin, address, mnemonic, error)) {
            displayMessage("Wallet Created", walletShortAddress(address));
            Serial.println("Wallet created successfully");
            Serial.print("Ethereum address: ");
            Serial.println(address);
            Serial.println("WARNING: seed phrase is shown only on device LCD.");
            delay(2200);

            displayMessage("Backup now", "Press button");
            while (encoderUpdate() != ENC_CLICK) delay(20);
            showMnemonicWords(mnemonic);
        } else {
            displayMessage("Create failed", error);
            Serial.print("Create failed: ");
            Serial.println(error);
            delay(2200);
        }
        return true;
    }

    else if (menuIndex == 1) { // Show Address
        WalletInfo info = walletGetInfo();
        if (!info.created) {
            displayMessage("No wallet", "Create first");
            delay(1500);
            return true;
        }
        showLongTextPages("Address", info.address);
        return true;
    }

    else if (menuIndex == 2) { // Show Balance
        WalletInfo info = walletGetInfo();
        if (!info.created) {
            displayMessage("No wallet", "Create first");
            delay(1500);
            return true;
        }

        balancePending = true;
        balanceRequestStartedMs = millis();

        displayMessage("Balance", "Waiting PC...");
        protocolSendBalanceRequest(info.address);

        Serial.println("Balance request sent to PC application");
        return false;
    }

    else if (menuIndex == 3) { // Send ETH
        WalletInfo info = walletGetInfo();
        if (!info.created) {
            displayMessage("No wallet", "Create first");
            delay(1500);
            return true;
        }

        sendSessionActive = true;
        sendSessionStartedMs = millis();
        displayMessage("Send ETH", "Open PC app");
        protocolSendOpenSendRequest(info.address);
        Serial.println("Send form request sent to PC application");
        return false;
    }

    else if (menuIndex == 4) { // Backup Seed
        WalletInfo info = walletGetInfo();
        if (!info.created) {
            displayMessage("No wallet", "Create first");
            delay(1500);
            return true;
        }

        String pin = enterPin("Backup PIN");
        String mnemonic, error;
        displayMessage("Decrypting", "Please wait");

        if (!walletGetMnemonic(pin, mnemonic, error)) {
            displayMessage("Backup failed", error);
            delay(1700);
            return true;
        }

        displayMessage("Seed phrase", "Keep offline!");
        delay(1600);
        showMnemonicWords(mnemonic);
        return true;
    }

    else if (menuIndex == 5) { // Device Info
        String sn = ateccSerialHex();
        bool ok = (sn != "UNAVAILABLE");
        displayMessage("ESP32 Wallet", String("SE:") + (ok ? "OK " : "NO ") + "FW:" FW_VERSION);
        Serial.println("Device Info");
        Serial.print("FW: "); Serial.println(FW_VERSION);
        Serial.print("ATECC: "); Serial.println(ok ? "OK" : "FAIL");
        Serial.print("Serial: "); Serial.println(sn);
        delay(2200);
        return true;
    }

    else if (menuIndex == 6) { // Wipe Wallet
        if (!walletGetInfo().created) {
            displayMessage("No wallet", "Nothing to wipe");
            delay(1400);
            return true;
        }

        String pin = enterPin("Wipe PIN");
        if (!walletVerifyPin(pin)) {
            displayMessage("Wrong PIN", "Wipe denied");
            delay(1600);
            return true;
        }

        displayMessage("Confirm wipe", "Press button");
        while (true) {
            EncoderEvent ev = encoderUpdate();
            if (ev == ENC_CLICK) break;
            if (ev == ENC_LEFT || ev == ENC_RIGHT) {
                displayMessage("Wipe canceled", "Done");
                delay(1200);
                return true;
            }
            delay(20);
        }

        walletErase();
        displayMessage("Wallet erased", "Done");
        Serial.println("Wallet erased");
        delay(1600);
        return true;
    }

    return true;
}

static void handleSerial() {
    String line;
    if (!protocolReadLine(line)) return;

    BalanceResponse balance;
    if (protocolParseBalanceResponse(line, balance)) {
        if (!balancePending) {
            Serial.println("Balance response ignored: no active request");
            return;
        }

        balancePending = false;
        String symbol = balance.symbol;
        if (symbol.indexOf("ETH") >= 0) symbol = "ETH";
        if (symbol.length() == 0) symbol = "ETH";

        String lcdBalance = balance.balance + " " + symbol;
        if (lcdBalance.length() > 16) {
            int maxAmountLen = 16 - 1 - symbol.length();
            if (maxAmountLen < 1) maxAmountLen = 1;
            lcdBalance = balance.balance.substring(0, maxAmountLen) + " " + symbol;
        }

        balanceViewActive = true;
        displayMessage("Balance", lcdBalance);
        Serial.print("Balance: ");
        Serial.print(balance.balance);
        Serial.print(" ");
        Serial.println(symbol);
        Serial.println("Balance screen is kept until encoder click");
        return;
    }

    String type = protocolJsonValue(line, "type");

    if (type == "incoming") {
        if (txPending) {
            Serial.println("Incoming notification postponed: TX is pending");
            return;
        }

        String amount = protocolJsonValue(line, "amount");
        String symbol = protocolJsonValue(line, "symbol");
        if (symbol.length() == 0) symbol = "ETH";
        if (symbol.indexOf("ETH") >= 0) symbol = "ETH";
        if (amount.length() == 0) amount = "?";

        notificationLine1 = "Incoming ETH";
        notificationLine2 = "+" + amount + " " + symbol;
        if (notificationLine2.length() > 16) notificationLine2 = notificationLine2.substring(0, 16);
        notificationActive = true;
        balancePending = false;
        balanceViewActive = false;
        sendSessionActive = false;

        displayMessage(notificationLine1, notificationLine2);
        Serial.print("Incoming transfer: +");
        Serial.print(amount);
        Serial.print(" ");
        Serial.println(symbol);
        return;
    }

    if (type == "tx_status") {
        String status = protocolJsonValue(line, "status");
        String hash = protocolJsonValue(line, "hash");
        if (status == "sent") {
            displayMessage("TX SENT", walletShortAddress(hash));
            delay(2500);
        } else if (status == "dry_run") {
            displayMessage("Dry run OK", "Not broadcast");
            delay(1800);
        } else {
            String err = protocolJsonValue(line, "error");
            if (err.length() == 0) err = "PC error";
            displayMessage("TX error", err.substring(0, 16));
            delay(2200);
        }
        showMenu();
        return;
    }

    if (protocolIsCmd(line, "address")) {
        WalletInfo info = walletGetInfo();
        if (!info.created) {
            protocolSendError("No wallet");
            return;
        }
        protocolSendAddressResponse(info.address);
        Serial.print("Address sent to PC: ");
        Serial.println(info.address);
        return;
    }

    if (protocolIsCmd(line, "ping")) {
        protocolSendInfo("pong FW " FW_VERSION);
        return;
    }

    if (txPending) return;

    TxRequest tx;
    if (!protocolParseTxRequest(line, tx)) {
        Serial.println("Unknown or invalid serial message");
        return;
    }

    WalletInfo info = walletGetInfo();
    if (!info.created) {
        Serial.println("No wallet created. Create wallet first.");
        protocolSendError("No wallet");
        displayMessage("No wallet", "Create first");
        delay(1400);
        showMenu();
        return;
    }

    if (!sendSessionActive) {
        protocolSendError("Open Send ETH on device first");
        displayMessage("Use menu", "Send ETH first");
        delay(1600);
        showMenu();
        return;
    }

    sendSessionActive = false;
    pendingTx = tx;
    txPending = true;

    displayMessage("Amount", tx.amount + " " + tx.coin);
    Serial.println("TX pending. Confirm on device.");
    Serial.print("Amount: "); Serial.println(tx.amount);
    Serial.print("Coin: "); Serial.println(tx.coin);
    Serial.print("To: "); Serial.println(tx.to);
    Serial.print("Fee: "); Serial.println(tx.fee);
}

static void handleBalanceWait(EncoderEvent ev) {
    if (millis() - balanceRequestStartedMs > 20000) {
        balancePending = false;
        displayMessage("Balance", "No PC response");
        Serial.println("Balance request timeout");
        delay(1500);
        showMenu();
        return;
    }

    if (ev == ENC_CLICK || ev == ENC_LEFT || ev == ENC_RIGHT) {
        balancePending = false;
        displayMessage("Balance", "Canceled");
        Serial.println("Balance request canceled by user");
        delay(900);
        showMenu();
        return;
    }
}

static void handleBalanceView(EncoderEvent ev) {
    if (ev == ENC_CLICK) {
        balanceViewActive = false;
        Serial.println("Balance screen closed by user");
        showMenu();
    }
}

static void handleNotification(EncoderEvent ev) {
    if (ev == ENC_CLICK) {
        notificationActive = false;
        notificationLine1 = "";
        notificationLine2 = "";
        Serial.println("Notification closed by user");
        showMenu();
    }
}

static void handleSendSession(EncoderEvent ev) {
    if (millis() - sendSessionStartedMs > 120000) {
        sendSessionActive = false;
        displayMessage("Send timeout", "Open again");
        Serial.println("Send session timeout");
        delay(1400);
        showMenu();
        return;
    }

    if (ev == ENC_CLICK || ev == ENC_LEFT || ev == ENC_RIGHT) {
        sendSessionActive = false;
        displayMessage("Send canceled", "Done");
        Serial.println("Send session canceled by user");
        delay(1000);
        showMenu();
        return;
    }
}

static void handleTxConfirm(EncoderEvent ev) {
    static int txPage = 0;

    auto showTxPage = [&]() {
        if (txPage == 0) displayMessage("Amount", pendingTx.amount + " " + pendingTx.coin);
        else if (txPage == 1) displayMessage("To", walletShortAddress(pendingTx.to));
        else if (txPage == 2) displayMessage("Fee", pendingTx.fee);
        else if (txPage == 3) displayMessage("Approve?", "Click=Sign");
        else if (txPage == 4) displayMessage("Reject?", "Click=Cancel");
    };

    if (ev == ENC_RIGHT || ev == ENC_LEFT) {
        txPage += (ev == ENC_RIGHT) ? 1 : -1;
        if (txPage < 0) txPage = 4;
        if (txPage > 4) txPage = 0;
        showTxPage();
        return;
    }

    if (ev == ENC_CLICK) {
        if (txPage < 3) {
            txPage++;
            showTxPage();
            return;
        }

        if (txPage == 4) {
            displayMessage("Rejected", "By user");
            Serial.println("Transaction rejected on device");
            protocolSendRejected();
            delay(1200);
            txPending = false;
            txPage = 0;
            showMenu();
            return;
        }

        //approve and sign
        if (pendingTx.hash.length() == 0) {
            displayMessage("TX error", "No hash");
            protocolSendError("No transaction hash");
            delay(1400);
            txPending = false;
            txPage = 0;
            showMenu();
            return;
        }

        String pin = enterPin("TX PIN");
        displayMessage("Signing", "Please wait");

        String r, s, error;
        uint8_t recid = 0;
        bool ok = walletSignHash(pin, pendingTx.hash, r, s, recid, error);

        if (!ok) {
            displayMessage("Sign failed", error.substring(0, 16));
            protocolSendError(error);
            delay(1800);
        } else {
            displayMessage("Signed", "Sending PC...");
            protocolSendTxSignature(r, s, recid);
            Serial.println("TX signature sent to PC");
            delay(1200);
        }

        txPending = false;
        txPage = 0;
        showMenu();
    }
}

static String enterPin(const String &title) {
    int digits[PIN_LENGTH] = {0, 0, 0, 0};
    int pos = 0;

    while (pos < PIN_LENGTH) {
        String line = "";
        for (int i = 0; i < PIN_LENGTH; i++) {
            if (i < pos) line += "*";
            else if (i == pos) line += String(digits[i]);
            else line += "_";
        }
        displayMessage(title, line);

        while (true) {
            EncoderEvent ev = encoderUpdate();
            if (ev == ENC_RIGHT) {
                digits[pos] = (digits[pos] + 1) % 10;
                break;
            }
            if (ev == ENC_LEFT) {
                digits[pos] = (digits[pos] + 9) % 10;
                break;
            }
            if (ev == ENC_CLICK) {
                pos++;
                break;
            }
            delay(5);
        }
    }

    String pin = "";
    for (int i = 0; i < PIN_LENGTH; i++) pin += String(digits[i]);
    return pin;
}

static void showLongTextPages(const String &title, const String &text) {
    const int pageWidth = 16;
    int pageCount = (text.length() + pageWidth - 1) / pageWidth;
    if (pageCount < 1) pageCount = 1;

    int page = 0;
    int lastPage = -1;

    while (true) {
        if (page != lastPage) {
            int start = page * pageWidth;
            String chunk = text.substring(start, start + pageWidth);
            displayMessage(title + " " + String(page + 1) + "/" + String(pageCount), chunk);
            lastPage = page;
        }

        EncoderEvent ev = encoderUpdate();
        if (ev == ENC_RIGHT && page < pageCount - 1) page++;
        if (ev == ENC_LEFT && page > 0) page--;
        if (ev == ENC_CLICK) break;
        delay(5);
    }
}

static int mnemonicWordCount(const String &mnemonic) {
    if (mnemonic.length() == 0) return 0;
    int count = 1;
    for (int i = 0; i < (int)mnemonic.length(); i++) {
        if (mnemonic[i] == ' ') count++;
    }
    return count;
}

static String getMnemonicWord(const String &mnemonic, int targetIndex) {
    int current = 0;
    int start = 0;

    for (int i = 0; i <= (int)mnemonic.length(); i++) {
        if (i == (int)mnemonic.length() || mnemonic[i] == ' ') {
            if (current == targetIndex) return mnemonic.substring(start, i);
            current++;
            start = i + 1;
        }
    }

    return "";
}

static void showMnemonicWords(const String &mnemonic) {
    int count = mnemonicWordCount(mnemonic);
    if (count == 0) {
        displayMessage("Seed error", "No words");
        delay(1500);
        return;
    }

    int index = 0;
    int lastIndex = -1;

    while (true) {
        if (index != lastIndex) {
            String title = "Word " + String(index + 1) + "/" + String(count);
            String word = getMnemonicWord(mnemonic, index);
            displayMessage(title, word);
            lastIndex = index;
        }

        EncoderEvent ev = encoderUpdate();
        if (ev == ENC_RIGHT && index < count - 1) index++;
        if (ev == ENC_LEFT && index > 0) index--;
        if (ev == ENC_CLICK) break;
        delay(5);
    }
}
