#pragma once
#include "UiFrame.h"
namespace psu {
struct DisplayStats {
  uint32_t renderMaxUs = 0, transactionMaxUs = 0;
  uint32_t sceneMs = 0, sceneMaxMs = 0;
};
// Device boundary: begin runs before CAN; service must release shared buses
// between bounded transfers. Navigation and business logic live elsewhere.
class Display {
 public:
  virtual bool begin() = 0;
  // Call after composing a new scene; an idle renderer must do no scanning.
  virtual void invalidate() = 0;
  virtual void service(const UiFrame& frame) = 0;
  virtual bool pending() const = 0; // True until the submitted scene is fully painted.
  virtual bool canDim() const = 0;
  virtual void dim(bool dimmed) = 0;
  virtual DisplayStats statistics() const { return {}; }
  virtual ~Display() = default;
};
}
