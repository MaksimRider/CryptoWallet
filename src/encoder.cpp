#include "encoder.h"
#include "config.h"

//алгоритм для KY-040/EC11(encoder):


static int lastCLK = HIGH;
static int lastButton = HIGH;
static uint32_t lastStepUs = 0;
static uint32_t lastClickMs = 0;

void encoderInit() {
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);

    lastCLK = digitalRead(ENCODER_CLK);
    lastButton = digitalRead(ENCODER_SW);
}

EncoderEvent encoderUpdate() {
    const uint32_t nowUs = micros();
    const uint32_t nowMs = millis();

    int currentCLK = digitalRead(ENCODER_CLK);

    // Один крок на falling edge CLK.
    if (lastCLK == HIGH && currentCLK == LOW) {
        // Дуже малий фільтр від дребезгу, не 70 ms.
        if ((nowUs - lastStepUs) > 1500) {
            lastStepUs = nowUs;

            bool dtHigh = digitalRead(ENCODER_DT) == HIGH;

            EncoderEvent ev = dtHigh ? ENC_RIGHT : ENC_LEFT;
#if ENCODER_REVERSE
            ev = (ev == ENC_RIGHT) ? ENC_LEFT : ENC_RIGHT;
#endif
            lastCLK = currentCLK;
            return ev;
        }
    }

    lastCLK = currentCLK;

    int currentButton = digitalRead(ENCODER_SW);
    if (lastButton == HIGH && currentButton == LOW && (nowMs - lastClickMs) > 220) {
        lastClickMs = nowMs;
        lastButton = currentButton;
        return ENC_CLICK;
    }

    lastButton = currentButton;
    return ENC_NONE;
}
