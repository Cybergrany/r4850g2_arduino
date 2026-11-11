#pragma once
#include <stdint.h>
namespace psu { namespace ui {
constexpr uint16_t refreshMs = 500, blinkMs = 500;
constexpr uint16_t voltageStep = 10, currentStep = 50; // 0.1 V / 0.5 A; limits remain in the backend.
constexpr uint32_t monitorPageMs = 5000, backlightIdleMs = 60000;
constexpr uint8_t dimBrightness = 24, fullBrightness = 255;
constexpr uint8_t groupsPerPage = 3, unitsPerPage = 2;
constexpr uint8_t columns = 26, rows = 15, cellWidth = 12, cellHeight = 16;
constexpr uint8_t outlineWidth = 2;
constexpr uint16_t backgroundColor = 0x10c2, outlineColor = 0x528a, focusColor = 0x07ff;
// Short SPI transactions, with a cooperative time budget between work units.
// The 500 kHz fallback retains one row/burst and at most two bursts/call.
constexpr uint16_t renderBudgetUs = 750;
// Foreground navigation can use consecutive slices, with CAN RX/protection
// serviced between each. Periodic metric updates retain one slice per loop.
constexpr uint16_t priorityRenderUs = 8000;
constexpr uint8_t priorityRenderSlices = 12;
constexpr uint8_t burstRows(uint32_t hz) { return hz >= 8000000UL ? 4 : hz >= 4000000UL ? 2 : 1; }
constexpr uint8_t burstsPerTick(uint32_t hz) { return hz > 2000000UL ? 8 : 2; }
// Check elapsed time after each small comparison batch, rather than requiring
// thirteen whole application loops to compare an unchanged 390-cell scene.
constexpr uint8_t scanCellsPerBatch = 8;
} }
