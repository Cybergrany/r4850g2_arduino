#include <Arduino.h>
#include "app/Application.h"

namespace { psu::Application application; }
void setup() { application.begin(); }
void loop() { application.tick(); }
