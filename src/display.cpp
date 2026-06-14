#include "display.h"
#include "config.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
static String currentLine1 = "";
static String currentLine2 = "";

static String fit16(const String &s) {
    if (s.length() <= 16) return s;
    return s.substring(0, 16);
}

static void printPaddedLine(uint8_t row, const String &text) {
    lcd.setCursor(0, row);
    String out = fit16(text);
    lcd.print(out);
    for (int i = out.length(); i < 16; i++) lcd.print(' ');
}

void displayInit() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    currentLine1 = "";
    currentLine2 = "";
}

void displayClear() {
    lcd.clear();
    currentLine1 = "";
    currentLine2 = "";
}

void displayMessage(const String &line1, const String &line2) {
    String l1 = fit16(line1);
    String l2 = fit16(line2);

    if (l1 == currentLine1 && l2 == currentLine2) return;

    if (l1 != currentLine1) {
        printPaddedLine(0, l1);
        currentLine1 = l1;
    }

    if (l2 != currentLine2) {
        printPaddedLine(1, l2);
        currentLine2 = l2;
    }
}

void displayMenu(const String &title, const String &item) {
    displayMessage(title, ">" + item);
}

void displayProgress(const String &title, const String &status) {
    displayMessage(title, status);
}
