#pragma once
#include <stdint.h>
#include "BuildOptions.h"

namespace psu {
namespace limits {
// Centivolts / centiamps keep settings exact and EEPROM records compact.
constexpr uint16_t minVoltage = 4150;
constexpr uint16_t maxVoltage = 5850;
constexpr uint16_t minOfflineVoltage = 4800;
constexpr uint16_t maxCurrent = 6000;
constexpr uint16_t defaultVoltage = 5520;
constexpr uint16_t defaultCurrent = 100;
constexpr uint16_t defaultRatedCurrent = 5000; // R4850G2 = 50 A
constexpr uint16_t defaultPollMs = 1000;
constexpr uint16_t minPollMs = 1000;
constexpr uint16_t maxPollMs = 10000;
constexpr uint16_t ackTimeoutMs = 750;
constexpr uint16_t commandGapMs = 250;
constexpr uint16_t staleMs = 5000;
}

struct PsuConfig {
  uint8_t address; // Huawei software address 1..127. 0 is broadcast, never a PSU.
  bool enabled;
  uint16_t voltage;
  uint16_t current;
  uint16_t offlineVoltage;
  uint16_t offlineCurrent;
  uint16_t ratedCurrent;
};

struct Configuration {
  uint8_t count;
  bool applyOnBoot;
  uint16_t pollMs;
  PsuConfig units[PSU_MAX_UNITS];
};

enum class Parameter : uint8_t {
  Voltage, Current, OfflineVoltage, OfflineCurrent, RatedCurrent, Address, Enabled
};

PsuConfig defaultPsuConfig(uint8_t index);
Configuration defaultConfiguration();
bool validConfig(const PsuConfig& config);
bool validConfig(const Configuration& config);
// Values are in V / A; Address and Enabled are integer-valued.
bool changeParameter(PsuConfig& config, Parameter parameter, float value);
}
