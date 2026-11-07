#pragma once
#include "../config/AlertConfig.h"
namespace psu {
class PsuActiveOutput {
 public:
  void begin();
  void write(bool active);
};
}
