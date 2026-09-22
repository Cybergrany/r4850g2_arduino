#pragma once
#include "../can/CanTransport.h"

namespace psu { namespace protocol {
constexpr uint8_t dataCommand = 0x40;
constexpr uint8_t infoCommand = 0x50;
constexpr uint8_t setCommand = 0x80;
constexpr uint8_t descriptionCommand = 0xd2;
constexpr uint8_t currentCommand = 0x11;
constexpr uint16_t fixedPointScale = 1024;
constexpr float ahCurrentDivisor = 20.0f;
constexpr float ahSampleSeconds = 0.377f;
enum Register : uint8_t {
  OnlineVoltage = 0, OfflineVoltage = 1, Overvoltage = 2,
  OnlineCurrent = 3, OfflineCurrent = 4
};
enum Metric : uint8_t {
  InputPower, InputFrequency, InputCurrent, OutputPower, Efficiency, OutputVoltage,
  CurrentCapacity, InputVoltage, OutputTemperature, InputTemperature, OutputCurrent,
  FilteredOutputCurrent, MetricCount
};

uint8_t address(uint32_t id);
uint8_t command(uint32_t id);
bool isReply(const CanFrame& frame);
bool isCurrentBroadcast(const CanFrame& frame);
uint32_t readBigEndian(const uint8_t* data);
CanFrame request(uint8_t address, uint8_t command = dataCommand);
CanFrame setting(uint8_t address, uint8_t reg, uint16_t value);
uint16_t encodeVoltage(uint16_t centivolts);
uint16_t encodeCurrent(uint16_t centiamps, uint16_t ratedCentiamps);
int8_t metric(uint8_t reg);
} }
