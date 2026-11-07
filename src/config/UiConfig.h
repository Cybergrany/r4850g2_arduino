#pragma once
#include <stdint.h>
namespace psu { namespace ui {
constexpr uint16_t refreshMs = 500, blinkMs = 500;
constexpr uint16_t voltageStep = 10, currentStep = 50; // 0.1 V / 0.5 A; limits remain in the backend.
constexpr uint32_t monitorPageMs = 5000, backlightIdleMs = 60000;
constexpr uint8_t dimBrightness = 24, fullBrightness = 255;
constexpr uint8_t groupsPerPage = 3, unitsPerPage = 2;
constexpr uint8_t columns = 26, rows = 15, cellWidth = 12, cellHeight = 16;
// At 500 kHz, 12 RGB565 pixels + up to 11 address/command bytes take 560 us
// on the wire. Release CAN's RX interrupt between bursts and limit loop work.
constexpr uint8_t slicesPerTick = 2, sliceRows = 1;
constexpr uint8_t scanCellsPerTick = 32;
} }
