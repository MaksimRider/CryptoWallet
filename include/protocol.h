#pragma once
#include <Arduino.h>

struct TxRequest {
    bool valid;
    String amount;
    String to;
    String coin;
    String fee;
    String hash;
};

struct BalanceResponse {
    bool valid;
    String balance;
    String symbol;
};

bool protocolReadLine(String &line);
String protocolJsonValue(const String &json, const String &key);

bool protocolParseTxRequest(const String &json, TxRequest &tx);
bool protocolParseBalanceResponse(const String &json, BalanceResponse &balance);
bool protocolIsCmd(const String &json, const String &cmdName);

// Backward-compatible helper; reads and parses one TX request from Serial.
bool protocolReadTxRequest(TxRequest &tx);

void protocolSendApproved();
void protocolSendRejected();
void protocolSendInfo(const String &msg);
void protocolSendError(const String &msg);
void protocolSendAddressResponse(const String &address);
void protocolSendBalanceRequest(const String &address);
void protocolSendOpenSendRequest(const String &address);
void protocolSendTxSignature(const String &r, const String &s, uint8_t recid);
