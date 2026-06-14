#pragma once
#include <Arduino.h>

void randomBytes(uint8_t *out, size_t len);
void sha256Bytes(const uint8_t *data, size_t len, uint8_t out32[32]);
void keccak256Bytes(const uint8_t *data, size_t len, uint8_t out32[32]);

String hexEncode(const uint8_t *data, size_t len);
String hexEncodeLower(const uint8_t *data, size_t len);
bool hexDecodeStrict(const String &hexString, uint8_t *out, size_t outLen);

bool aes256CbcEncrypt16(const uint8_t key32[32], const uint8_t iv16[16], const uint8_t plain16[16], uint8_t cipher16[16]);
bool aes256CbcDecrypt16(const uint8_t key32[32], const uint8_t iv16[16], const uint8_t cipher16[16], uint8_t plain16[16]);
