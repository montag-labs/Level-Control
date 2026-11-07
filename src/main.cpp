#include <Arduino.h>
#include "App.h"

// Der Arduino-Core markiert ein frisch per OTA installiertes Image sonst schon vor setup() als gueltig und
// macht den Rollback damit wirkungslos. Die Bestaetigung erfolgt stattdessen in App::loop() nach einer
// stabilen Laufzeit (App::markImageValidWhenStable).
extern "C" bool verifyRollbackLater() {
    return true;
}

void setup() {
    App::getInstance().init();
}

void loop() {
    App::getInstance().loop();
}
