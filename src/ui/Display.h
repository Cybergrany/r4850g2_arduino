#pragma once
#include "UiFrame.h"
namespace psu {
// Device boundary: begin runs before CAN; service must release shared buses
// between bounded transfers. Navigation and business logic live elsewhere.
class Display {
 public:
  virtual bool begin() = 0;
  // Call after composing a new scene; an idle renderer must do no scanning.
  virtual void invalidate() = 0;
  virtual void service(const UiFrame& frame) = 0;
  virtual bool canDim() const = 0;
  virtual void dim(bool dimmed) = 0;
  virtual ~Display() = default;
};
}
