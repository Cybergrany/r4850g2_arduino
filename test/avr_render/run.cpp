#include <sim_avr.h>
#include <sim_elf.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// Use the AVR instruction clock, including Timer0 interrupts. TFT transfers
// model nominal wire cost; physical CAN/SPI/encoder/serial interrupt load is not
// simulated. Host renderer tests independently verify every final pixel.
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  elf_firmware_t firmware = {};
  if (elf_read_firmware(argv[1], &firmware)) return 2;
  avr_t* avr = avr_make_mcu_by_name("atmega2560");
  avr_init(avr); avr->frequency = 16000000; avr_load_firmware(avr, &firmware);
  unsigned marker = 0;
  for (unsigned i = 0; i < firmware.symbolcount; ++i)
    if (!std::strcmp(firmware.symbol[i]->symbol, "benchPhase")) marker = firmware.symbol[i]->addr & 0xffff;
  if (!marker) return 3;
  unsigned phase = 0, counts[16] = {};
  avr_cycle_count_t started = 0, elapsed[16] = {};
  while (avr->cycle < 3000000000ULL) {
    const int state = avr_run(avr);
    const auto next = avr->data[marker];
    if (next >= 16 || state == cpu_Done || state == cpu_Crashed) return 4;
    if (next != phase) {
      elapsed[phase] += avr->cycle - started; ++counts[phase];
      started = avr->cycle; phase = next;
      if (phase == 15) break;
    }
  }
  if (phase != 15) return 5;
  const char* names[] = {"Group highlight", "Charger highlight", "Tools highlight", "Group to charger page"};
  for (unsigned i = 0; i < 4; ++i) {
    const auto old = 2 + i * 2, current = old + 1;
    const double oldMs = elapsed[old] / 16000.0, currentMs = elapsed[current] / 16000.0;
    std::printf("%s: one-slice=%.1f ms priority=%.1f ms (AVR CPU + nominal SPI)\n", names[i], oldMs, currentMs);
    if (counts[old] != 1 || counts[current] != 1 || currentMs > oldMs / 3 || currentMs > (i == 2 ? 100 : 900)) return 6;
  }
  std::puts("PASS: complete redraws; physical loaded-bus latency remains a bench check");
}
