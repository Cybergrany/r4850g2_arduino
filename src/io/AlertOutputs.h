#pragma once
#include "../psu/Alerts.h"
namespace psu {
class AlertOutputs {
 public:
  void begin();
  void write(const AlertManager& state);
};
}
