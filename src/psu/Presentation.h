#pragma once
#include "PsuController.h"
namespace psu {
enum class Aggregate : uint8_t { Sum, Mean, Maximum };
struct Reading {
  float value;
  uint8_t observed, expected;
  bool valid() const { return observed != 0; }
  bool partial() const { return observed < expected; }
};
// Presentation helpers are shared backend policy, independent of graphics/input.
Reading summarize(const PsuController& c, uint8_t mask, protocol::Metric metric,
                  Aggregate aggregate, uint32_t now);
Reading sessionTotal(const PsuController& c, uint8_t mask);
bool pendingSettings(const PsuController& c, uint8_t mask);
bool commandFailed(CommandState state);
bool commandActive(CommandState state);
}
