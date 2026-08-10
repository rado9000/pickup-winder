#include <Arduino.h>

#include "app.h"

static App app;

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
