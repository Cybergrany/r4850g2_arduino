#pragma once
#include "../config/ControlConfig.h"
#if PSU_CHG_ALLOW_PIN >= 0 && !PSU_UI_DEBUG
namespace psu {
class ChargeAllowOutput {
 public:
  void begin();
  void write(bool allowed);
 private:
  bool allowed_ = false;
};
}
#endif
