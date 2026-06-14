#pragma once
#include <Arduino.h>

// Firmware
#define FW_VERSION "1.2.0"

// I2C
#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_FREQ 100000
#define ATECC_BOOT_RETRIES 8
#define ATECC_RETRY_DELAY_MS 120

// Modules
#define LCD_ADDR 0x27
#define ATECC_ADDR 0x60

// Encoder KY-040 / EC11
#define ENCODER_CLK 32
#define ENCODER_DT  33
#define ENCODER_SW  25

// Якщо напрямок меню буде навпаки — зміни 0 на 1.
#define ENCODER_REVERSE 0

// Wallet
#define PIN_LENGTH 4
#define ETH_DERIVATION_PATH "m/44'/60'/0'/0/0"
