#pragma once
#include "../config/ChargerConfig.h"
namespace psu {
enum class ThermalLevel : uint8_t { Normal, Warning, Error };
struct ThermalStatus {
  float maximum = 0;
  uint32_t hotMs = 0;
  UnitMask valid = 0, contributors = 0, sensorFaults = 0, ignored = 0;
  uint8_t percent = 100;
  ThermalLevel level = ThermalLevel::Normal;
  ThermalStop stop = ThermalStop::None;
  bool cool = false, saving = false, persisted = true, saveFailed = false, resuming = false;
};
}
