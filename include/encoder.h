#pragma once
#include <Arduino.h>

enum EncoderEvent {
    ENC_NONE,
    ENC_LEFT,
    ENC_RIGHT,
    ENC_CLICK
};

void encoderInit();
EncoderEvent encoderUpdate();
