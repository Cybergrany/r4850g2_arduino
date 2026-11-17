#pragma once
#include <stdint.h>
#ifndef PSU_CURRENT_SYNC_PRIME_CA
#define PSU_CURRENT_SYNC_PRIME_CA 100
#endif
#ifndef PSU_CURRENT_SYNC_TOLERANCE_PERCENT
#define PSU_CURRENT_SYNC_TOLERANCE_PERCENT 30
#endif
#ifndef PSU_CURRENT_SYNC_STABLE_MS
#define PSU_CURRENT_SYNC_STABLE_MS 2000UL
#endif
#ifndef PSU_CURRENT_SYNC_TIMEOUT_MS
#define PSU_CURRENT_SYNC_TIMEOUT_MS 300000UL
#endif
#ifndef PSU_CURRENT_BATCH_GAP_MS
#define PSU_CURRENT_BATCH_GAP_MS 25
#endif
namespace psu { namespace currentSync {
constexpr uint16_t primeCa = PSU_CURRENT_SYNC_PRIME_CA;
constexpr uint8_t tolerance = PSU_CURRENT_SYNC_TOLERANCE_PERCENT;
constexpr uint32_t stableMs = PSU_CURRENT_SYNC_STABLE_MS, timeoutMs = PSU_CURRENT_SYNC_TIMEOUT_MS;
constexpr uint16_t commandGapMs = PSU_CURRENT_BATCH_GAP_MS;
static_assert(PSU_CURRENT_SYNC_PRIME_CA > 0 && PSU_CURRENT_SYNC_PRIME_CA <= 100, "Priming must be positive and at most 1 A");
static_assert(PSU_CURRENT_SYNC_TOLERANCE_PERCENT > 0 && PSU_CURRENT_SYNC_TOLERANCE_PERCENT < 100, "Zero cannot qualify current sync");
static_assert(stableMs > 0 && timeoutMs > stableMs && timeoutMs < 0x80000000UL, "Invalid sync timing");
static_assert(commandGapMs >= 25 && commandGapMs <= 250, "Current batches require bounded pacing");
} }
