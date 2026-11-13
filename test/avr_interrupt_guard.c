// Optional simavr fixture for the built Mega firmware, not a replacement driver.
// Model an idle MCP2515 whose INT wire is held LOW, and just enough TFT/SPI/UART
// to boot the real application. No PSU replies or EEPROM configuration supplied.
// simavr 1.6's SPI timing is not a physical display/receive latency measurement.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sim_avr.h>
#include <sim_elf.h>
#include <sim_io.h>
#include <avr_ioport.h>
#include <avr_spi.h>
#include <avr_uart.h>

static avr_t *avr;
static avr_irq_t *spi_input;
static unsigned can_cs=1, tft_cs=1, dc=1, phase, command, address, mask;
static unsigned tft_command, tft_index, registers[128];
static unsigned tft_bytes, uart_bytes, isrs;
static avr_cycle_count_t boot_cycle;
static char line[160];
static unsigned line_size;

static void select_can(struct avr_irq_t *irq, uint32_t value, void *context) {
  (void)irq; (void)context;
  can_cs=value&1;
  if (!can_cs) { phase=0; command=0; }
}
static void select_tft(struct avr_irq_t *irq, uint32_t value, void *context) {
  (void)irq; (void)context; tft_cs=value&1;
}
static void set_dc(struct avr_irq_t *irq, uint32_t value, void *context) {
  (void)irq; (void)context; dc=value&1;
}
static void transfer(struct avr_irq_t *irq, uint32_t value, void *context) {
  (void)irq; (void)context;
  unsigned reply=0;
  if (!can_cs) {
    if (!phase++) {
      command=value;
      if (command==0xc0) memset(registers,0,sizeof(registers));
    } else if (phase==2) address=value;
    else if (command==3) reply=registers[address++ & 127];
    else if (command==2) registers[address++ & 127]=value;
    else if (command==5) {
      if (phase==3) mask=value;
      else registers[address]=(registers[address]&~mask)|(value&mask);
    }
  } else if (!tft_cs) {
    ++tft_bytes;
    if (!dc) tft_command=value;
    else if (tft_command==0xd9) tft_index=value;
    else if (tft_command==0xd3) reply=tft_index==0x12 ? 0x93 : 0x41;
  }
  avr_raise_irq(spi_input,reply);
}
static void serial_output(struct avr_irq_t *irq, uint32_t value, void *context) {
  (void)irq; (void)context;
  const char c=value&255; ++uart_bytes; putchar(c);
  if (line_size<sizeof(line)-1) line[line_size++]=c;
  line[line_size]=0;
  if (c=='\n') {
    if (!boot_cycle && strstr(line,"Startup complete")) boot_cycle=avr->cycle;
    line_size=0;
  }
}
static unsigned symbol(const elf_firmware_t *firmware, const char *name) {
  for (unsigned i=0;i<firmware->symbolcount;++i)
    if (!strcmp(firmware->symbol[i]->symbol,name)) return firmware->symbol[i]->addr;
  return ~0u;
}
int main(int argc, char **argv) {
  if (argc!=2) return 2;
  elf_firmware_t firmware={0};
  if (elf_read_firmware(argv[1],&firmware)<0) return 3;
  avr=avr_make_mcu_by_name("atmega2560"); avr_init(avr);
  avr->frequency=16000000; avr_load_firmware(avr,&firmware);
  // Mega D2=PE4 held LOW. Wheel inputs PE3/PE5/PG5 remain released.
  avr_ioport_external_t external={.name='E',.mask=0x38,.value=0x28};
  avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('E'),&external);
  external.name='G'; external.mask=0x20; external.value=0x20;
  avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('G'),&external);
  spi_input=avr_io_getirq(avr,AVR_IOCTL_SPI_GETIRQ(0),SPI_IRQ_INPUT);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_SPI_GETIRQ(0),SPI_IRQ_OUTPUT),transfer,0);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),4),select_can,0);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),0),select_tft,0);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),1),set_dc,0);
  uint32_t uart_flags=0; avr_ioctl(avr,AVR_IOCTL_UART_SET_FLAGS('0'),&uart_flags);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_OUTPUT),serial_output,0);
  avr_int_vector_t *int4=0;
  for (unsigned i=0;i<avr->interrupts.vector_count;++i)
    if (avr->interrupts.vector[i]->vector==5) int4=avr->interrupts.vector[i];
  const unsigned handler=symbol(&firmware,"_ZN3psu16Mcp2515Transport11onInterruptEv");
  const unsigned timer=symbol(&firmware,"timer0_millis")&0xffff;
  if (!int4 || handler==~0u || timer==0xffff) return 4;
  const char *input="diag bus\n"; unsigned sent=0;
  avr_cycle_count_t next_input=0;
  while (avr->cycle<16000000ULL*30 && avr->state!=cpu_Crashed && avr->state!=cpu_Done) {
    if (avr->pc==handler) ++isrs;
    if (boot_cycle && !next_input) next_input=boot_cycle+16000000;
    if (next_input && avr->cycle>=next_input && input[sent]) {
      avr_raise_irq(avr_io_getirq(avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_INPUT),input[sent++]);
      next_input+=160000; // One character per 10 ms.
    }
    // Model the ATmega's LOW-level INT4 retrigger, absent in simavr 1.6's PE4
    // model. Obey EIMSK and EICRB; the production detach must stop this source.
    if ((avr->data[0x3d]&0x10) && !(avr->data[0x6a]&3)) avr_raise_interrupt(avr,int4);
    avr_run(avr);
    if (boot_cycle && avr->cycle-boot_cycle>=16000000ULL*5) break;
  }
  const uint32_t ms=avr->data[timer] | (uint32_t)avr->data[timer+1]<<8 |
      (uint32_t)avr->data[timer+2]<<16 | (uint32_t)avr->data[timer+3]<<24;
  printf("\nSIM_RESULT cycles=%llu millis=%u interrupts=%u uart-bytes=%u tft-bytes=%u boot=%u\n",
      (unsigned long long)avr->cycle,ms,isrs,uart_bytes,tft_bytes,boot_cycle!=0);
  return 0;
}
