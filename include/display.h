#pragma once
#include <Arduino.h>

void displayInit();
void displayClear();
void displayMessage(const String &line1, const String &line2 = "");
void displayMenu(const String &title, const String &item);
void displayProgress(const String &title, const String &status);
