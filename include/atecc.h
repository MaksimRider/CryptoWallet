#pragma once
#include <Arduino.h>

void ateccInit();
bool ateccIsPresent();
bool ateccWake();
bool ateccReadSerial(uint8_t serial9[9]);
String ateccSerialHex();
bool ateccRandom32(uint8_t out32[32]);
