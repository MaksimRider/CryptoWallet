#include "atecc.h"
#include "config.h"
#include <Wire.h>

// Мінімальний raw-драйвер ATECC608 без.


static String cachedSerial = "";
static bool cachedReady = false;
static uint32_t lastCheckMs = 0;

static void atcaCrc16(size_t length, const uint8_t *data, uint8_t crc[2]) {
    uint16_t crcReg = 0;

    for (size_t counter = 0; counter < length; counter++) {
        for (uint8_t shift = 0x01; shift > 0x00; shift <<= 1) {
            uint8_t dataBit = (data[counter] & shift) ? 1 : 0;
            uint8_t crcBit = (crcReg >> 15) & 0x01;
            crcReg <<= 1;
            if (dataBit != crcBit) crcReg ^= 0x8005;
        }
    }

    crc[0] = (uint8_t)(crcReg & 0xFF);
    crc[1] = (uint8_t)((crcReg >> 8) & 0xFF);
}

static bool checkResponseCrc(const uint8_t *resp, size_t len) {
    if (len < 3) return false;
    uint8_t crc[2];
    atcaCrc16(len - 2, resp, crc);
    return crc[0] == resp[len - 2] && crc[1] == resp[len - 1];
}

static bool rawI2CPresent() {
    Wire.beginTransmission(ATECC_ADDR);
    return Wire.endTransmission() == 0;
}

static bool wakeToken() {
    Wire.beginTransmission(0x00);
    Wire.endTransmission();
    delayMicroseconds(1800);

    uint8_t resp[4] = {0};
    size_t got = Wire.requestFrom((uint8_t)ATECC_ADDR, (uint8_t)4);

    size_t i = 0;
    while (Wire.available() && i < sizeof(resp)) {
        resp[i++] = Wire.read();
    }


    if (got == 4 && i == 4 && resp[0] == 0x04) return true;
    return rawI2CPresent();
}

static bool sendCommand(uint8_t opcode, uint8_t param1, uint16_t param2, uint16_t execDelayMs) {
    uint8_t cmd[7];
    cmd[0] = 7; 
    cmd[1] = opcode;
    cmd[2] = param1;
    cmd[3] = (uint8_t)(param2 & 0xFF);
    cmd[4] = (uint8_t)((param2 >> 8) & 0xFF);
    atcaCrc16(5, cmd, &cmd[5]);

    Wire.beginTransmission(ATECC_ADDR);
    Wire.write(0x03); 
    Wire.write(cmd, sizeof(cmd));
    if (Wire.endTransmission() != 0) return false;

    delay(execDelayMs);
    return true;
}

static bool readResponse(uint8_t *buf, size_t expectedLen, uint16_t timeoutMs = 150) {
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        size_t got = Wire.requestFrom((uint8_t)ATECC_ADDR, (uint8_t)expectedLen);
        if (got > 0) {
            size_t i = 0;
            while (Wire.available() && i < expectedLen) {
                buf[i++] = Wire.read();
            }
            if (i >= 1 && buf[0] == i && checkResponseCrc(buf, i)) return true;
        }
        delay(5);
    }

    return false;
}

static bool readSerialNoCache(uint8_t serial9[9]) {
    for (int attempt = 0; attempt < ATECC_BOOT_RETRIES; attempt++) {
        if (!wakeToken()) {
            delay(ATECC_RETRY_DELAY_MS);
            continue;
        }

    
        if (!sendCommand(0x02, 0x80, 0x0000, 5)) {
            delay(ATECC_RETRY_DELAY_MS);
            continue;
        }

        uint8_t resp[35] = {0};
        if (!readResponse(resp, sizeof(resp), 180)) {
            delay(ATECC_RETRY_DELAY_MS);
            continue;
        }

        const uint8_t *cfg = &resp[1];
        serial9[0] = cfg[0];
        serial9[1] = cfg[1];
        serial9[2] = cfg[2];
        serial9[3] = cfg[3];
        serial9[4] = cfg[8];
        serial9[5] = cfg[9];
        serial9[6] = cfg[10];
        serial9[7] = cfg[11];
        serial9[8] = cfg[12];
        return true;
    }

    return false;
}

void ateccInit() {
    cachedSerial = "";
    cachedReady = false;
    lastCheckMs = 0;

    delay(350);
}

bool ateccIsPresent() {
    if (cachedReady && (millis() - lastCheckMs) < 10000) return true;

    uint8_t sn[9];
    bool ok = readSerialNoCache(sn);
    lastCheckMs = millis();
    cachedReady = ok;

    if (ok && cachedSerial.length() == 0) {
        String out;
        for (int i = 0; i < 9; i++) {
            if (sn[i] < 0x10) out += "0";
            out += String(sn[i], HEX);
        }
        out.toUpperCase();
        cachedSerial = out;
    }

    return ok;
}

bool ateccWake() {
    for (int attempt = 0; attempt < ATECC_BOOT_RETRIES; attempt++) {
        if (wakeToken()) return true;
        delay(ATECC_RETRY_DELAY_MS);
    }
    return false;
}

bool ateccReadSerial(uint8_t serial9[9]) {
    bool ok = readSerialNoCache(serial9);
    lastCheckMs = millis();
    cachedReady = ok;

    if (ok) {
        String out;
        for (int i = 0; i < 9; i++) {
            if (serial9[i] < 0x10) out += "0";
            out += String(serial9[i], HEX);
        }
        out.toUpperCase();
        cachedSerial = out;
    }

    return ok;
}

String ateccSerialHex() {
    if (cachedSerial.length() > 0) return cachedSerial;

    uint8_t sn[9];
    if (!ateccReadSerial(sn)) return "UNAVAILABLE";
    return cachedSerial;
}

bool ateccRandom32(uint8_t out32[32]) {
    for (int attempt = 0; attempt < ATECC_BOOT_RETRIES; attempt++) {
        if (!wakeToken()) {
            delay(ATECC_RETRY_DELAY_MS);
            continue;
        }

        if (!sendCommand(0x1B, 0x00, 0x0000, 30)) {
            delay(ATECC_RETRY_DELAY_MS);
            continue;
        }

        uint8_t resp[35] = {0};
        if (readResponse(resp, sizeof(resp), 180)) {
            memcpy(out32, &resp[1], 32);
            return true;
        }

        delay(ATECC_RETRY_DELAY_MS);
    }

    return false;
}
