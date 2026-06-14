#include "protocol.h"

static String jsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

bool protocolReadLine(String &line) {
    if (!Serial.available()) return false;

    line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return false;

    Serial.print("RX: ");
    Serial.println(line);
    return true;
}

String protocolJsonValue(const String &json, const String &key) {
    String searchKey = "\"" + key + "\"";
    int keyIndex = json.indexOf(searchKey);
    if (keyIndex < 0) return "";

    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex < 0) return "";

    int firstQuote = json.indexOf("\"", colonIndex + 1);
    if (firstQuote < 0) return "";

    int secondQuote = json.indexOf("\"", firstQuote + 1);
    if (secondQuote < 0) return "";

    return json.substring(firstQuote + 1, secondQuote);
}

bool protocolIsCmd(const String &json, const String &cmdName) {
    String cmd = protocolJsonValue(json, "cmd");
    return cmd == cmdName;
}

bool protocolParseTxRequest(const String &json, TxRequest &tx) {
    tx.valid = false;

    tx.amount = protocolJsonValue(json, "amount");
    tx.to = protocolJsonValue(json, "to");
    tx.coin = protocolJsonValue(json, "coin");
    tx.fee = protocolJsonValue(json, "fee");
    tx.hash = protocolJsonValue(json, "hash");

    if (tx.coin == "") tx.coin = "ETH";
    if (tx.fee == "") tx.fee = "?";

    tx.valid = tx.amount.length() > 0 && tx.to.length() > 0;
    return tx.valid;
}

bool protocolParseBalanceResponse(const String &json, BalanceResponse &balance) {
    balance.valid = false;

    String type = protocolJsonValue(json, "type");
    String cmd = protocolJsonValue(json, "cmd");

    if (type != "balance" && cmd != "balance") return false;

    balance.balance = protocolJsonValue(json, "balance");
    balance.symbol = protocolJsonValue(json, "symbol");
    if (balance.symbol == "") balance.symbol = "ETH";
    if (balance.symbol.indexOf("ETH") >= 0) balance.symbol = "ETH";

    balance.valid = balance.balance.length() > 0;
    return balance.valid;
}

bool protocolReadTxRequest(TxRequest &tx) {
    String line;
    if (!protocolReadLine(line)) {
        tx.valid = false;
        return false;
    }
    return protocolParseTxRequest(line, tx);
}

void protocolSendApproved() {
    Serial.println("{\"status\":\"approved\",\"signature\":\"TEST_SIGNATURE\"}");
}

void protocolSendRejected() {
    Serial.println("{\"status\":\"rejected\"}");
}

void protocolSendInfo(const String &msg) {
    Serial.print("{\"info\":\"");
    Serial.print(jsonEscape(msg));
    Serial.println("\"}");
}

void protocolSendError(const String &msg) {
    Serial.print("{\"type\":\"error\",\"error\":\"");
    Serial.print(jsonEscape(msg));
    Serial.println("\"}");
}

void protocolSendAddressResponse(const String &address) {
    Serial.print("{\"type\":\"address\",\"address\":\"");
    Serial.print(jsonEscape(address));
    Serial.println("\"}");
}

void protocolSendBalanceRequest(const String &address) {
    Serial.print("{\"cmd\":\"balance\",\"address\":\"");
    Serial.print(jsonEscape(address));
    Serial.println("\"}");
}


void protocolSendOpenSendRequest(const String &address) {
    Serial.print("{\"cmd\":\"open_send\",\"address\":\"");
    Serial.print(jsonEscape(address));
    Serial.println("\"}");
}

void protocolSendTxSignature(const String &r, const String &s, uint8_t recid) {
    Serial.print("{\"type\":\"tx_signature\",\"status\":\"signed\",\"r\":\"");
    Serial.print(jsonEscape(r));
    Serial.print("\",\"s\":\"");
    Serial.print(jsonEscape(s));
    Serial.print("\",\"recid\":");
    Serial.print(recid);
    Serial.println("}");
}
