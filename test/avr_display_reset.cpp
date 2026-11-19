// Runs the actual Mega wheel ELF: nine synthetic Huawei peers behind a two-
// buffer MCP2515, real AVR interrupts/UART/EEPROM, recording TFT commands.
// PSU responses are a test model, not evidence of physical PSU behaviour.
// simavr 1.6 uses 100 us/byte regardless of SPI divider; replace only its SPDR
// callbacks with a divider-timed master transfer. SPI waveform edges/electrical
// faults and PSU ramp dynamics remain outside this fixture.
// simavr 1.6 also leaves Timer1 stopped in TimerOne's WGM8 mode. Supply that
// configured 1 ms overflow; the firmware ISR and ClickEncoder still execute
// normally. Reject an unexpected timer setup or doubled native IRQ cadence.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <algorithm>
extern "C" {
#include <sim_avr.h>
#include <sim_elf.h>
#include <sim_io.h>
#include <avr_ioport.h>
#include <avr_uart.h>
#include <avr_eeprom.h>
}
using Cycle=avr_cycle_count_t;
static constexpr Cycle mhz=16, second=16000000;
static avr_t* avr;
static avr_irq_t *irq_pin,*uart_in;
static unsigned regs[128],can_cs=1,tft_cs=1,dc=1,phase,command,address,mask;
static unsigned tft_cmd,tft_index,tft_bytes,overflows,received,transmitted;
static unsigned permit=0,active=0,zero_after=0,standby_after=0,permit_falls=0,active_falls=0;
static unsigned target[9],voltage[9],fan[9],standby[9];
static Cycle boot=0,stable=0,trigger=0,reset_low=0,reset_end=0,off_at=0,first_zero=0,last_zero=0,all_zero=0;
static unsigned during_received=0,delayed_ack=0,save_sent=0;
static unsigned reset_count=0,gpio_step=0,unwanted_current=0;
static unsigned encoder_calls=0;
static bool editor_preserved=false,ordinary_hold_action=false;
static std::string scene,held_scene;
static bool timeout_reported=false;
static Cycle recovering_at=0,clear_at=0,paint_at=0,recovery_gap=0,recovery_loop=0,recovery_cs=0;
static unsigned reset_state=0,init_hash=0,boot_hash=0,reset_hash=0,capture_init=0;
static Cycle last_rx=0,max_rx_gap=0,last_loop=0,max_loop_gap=0,cs_start=0,max_cs=0;
static Cycle before_rx=0,before_loop=0,before_cs=0,next_tick=0,next_uart=0,wire_free=0;
static unsigned min_sp=0xffff,reset_min_sp=0xffff,mode=0,inject=0,after_measure=0;
static unsigned tft_parameter=0,clear_pixels=0,paint_pixels=0,reset_level=1,shared_cs=0;
static unsigned x1=0,x2=239,y1=0,y2=319,px=0,py=0,madctl=0,window_byte=0,window_errors=0;
static bool have_x=false,have_y=false;
static Cycle first_paint=0,last_paint=0;
static Cycle next_encoder=16000;
static std::string input,line;
struct Frame { uint32_t id; uint8_t data[8]; };
struct Arrival { Cycle at; Frame frame; bool operator<(const Arrival& b)const{return at>b.at;} };
static std::priority_queue<Arrival> arrivals;
static Frame receive_buffers[2];
static Cycle broadcasts[9];
static unsigned read_frame=0;
static unsigned symbol(const elf_firmware_t& fw,const char* prefix) {
  for(unsigned i=0;i<fw.symbolcount;++i)
    if(!strncmp(fw.symbol[i]->symbol,prefix,strlen(prefix))) return fw.symbol[i]->addr;
  return ~0U;
}
static void irq_level() { avr_raise_irq(irq_pin,(regs[0x2b]&regs[0x2c]&3)?0:1); }
static Frame reply(unsigned unit,unsigned cmd) {
  Frame f={}; f.id=0x1080007eU | (unit+1)<<16 | cmd<<8; return f;
}
static void big(uint8_t* out,uint32_t value) { for(unsigned i=0;i<4;++i)out[i]=value>>(24-8*i); }
static void queue(Frame f,Cycle delay=2000*mhz) {
  arrivals.push({avr->cycle+delay,f});
}
static Frame metric(unsigned i,unsigned field) {
  static const uint8_t fields[]={0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x78,0x7f,0x80,0x81,0x82,0x83};
  auto f=reply(i,0x40);f.data[0]=1;f.data[1]=fields[field];
  const uint32_t amps=standby[i]?0:target[i]*5000/1024;
  const uint32_t watts=(voltage[i]*100U/1024)*amps/10000;
  uint32_t raw=0;
  switch(field) {
    case 0: raw=watts*1100;break; case 1:raw=50*1024;break;
    case 2:raw=watts*1024/210;break; case 3:raw=watts*1024;break;
    case 4:raw=930;break;case 5:raw=voltage[i];break;case 6:raw=50*1024;break;
    case 7:raw=230*1024;break;case 8:case 9:raw=(mode==3&&off_at&&i==0?76:25)*1024;break;
    case 10:case 11:raw=amps*1024/100;break;case 12:raw=0;break;
  }
  big(f.data+4,raw);return f;
}
static void sent() {
  ++transmitted;
  const uint32_t id=regs[0x31]<<21 | (regs[0x32]&0xe0)<<13 | (regs[0x32]&3)<<16 | regs[0x33]<<8 | regs[0x34];
  const unsigned addr=(id>>16)&0x7f,cmd=(id>>8)&255;
  regs[0x30]&=~8;regs[0x2c]|=4;
  if(!addr || addr>9) return;
  const unsigned i=addr-1;
  if(cmd==0x50) { auto f=reply(i,cmd);f.data[1]=2; const uint8_t identity[]={0x53,0x49,0x4d,0,0,uint8_t(addr)};memcpy(f.data+2,identity,6);queue(f); }
  if(cmd==0x40)for(unsigned k=0;k<13;++k)queue(metric(i,k),(2000+1400*k)*mhz);
  if(cmd==0x80) {
    auto f=reply(i,cmd);for(unsigned k=0;k<8;++k)f.data[k]=regs[0x36+k];
    const unsigned reg=f.data[1],raw=f.data[6]*256+f.data[7];
    if(reg==0)voltage[i]=raw;
    if(reg==3) {
      target[i]=raw;
      if(trigger&&mode>=7&&raw!=204)++unwanted_current;
      if(trigger&&!raw) { ++zero_after;if(!first_zero)first_zero=avr->cycle;last_zero=avr->cycle; }
    }
    if(reg==0x32) {standby[i]=f.data[3];if(trigger&&f.data[3])++standby_after;}
    if(reg==0x34)fan[i]=f.data[3];
    // Begin recovery while a current ACK is outstanding. A 600 ms response is
    // intentionally near the 750 ms deadline; all normal responses take 2 ms.
    const bool pending=(mode==4||mode==5)&&stable&&!trigger&&reg==3&&raw>204;
    if(pending) { input+="display reset\n"; trigger=avr->cycle; next_uart=avr->cycle;delayed_ack=1; }
    if(!(pending&&mode==5))queue(f,pending?600000*mhz:2000*mhz);
    if(trigger)printf("MODEL TX at=%.3f unit=%u reg=%u raw=%u toggle=%u\n",double(avr->cycle)/second,addr,reg,raw,f.data[3]);
  }
}
static void reg_write(unsigned a,unsigned v) {
  regs[a]=v;
  if(a==0x30&&(v&8))sent();
  if(a==0x2b||a==0x2c)irq_level();
}
static void select_can(avr_irq_t*,uint32_t value,void*) {
  if((value&1)&&!can_cs&&(command==0x90||command==0x94)) {regs[0x2c]&=~(1U<<read_frame);irq_level();}
  can_cs=value&1;if(!can_cs){phase=command=0;if(!tft_cs)++shared_cs;}
}
static void select_tft(avr_irq_t*,uint32_t value,void*) {
  const unsigned next=value&1;
  if(!next&&tft_cs){cs_start=avr->cycle;if(!can_cs)++shared_cs;}
  if(next&&!tft_cs&&stable){const auto gap=avr->cycle-cs_start;(trigger?max_cs:before_cs)=std::max(trigger?max_cs:before_cs,gap);if(reset_state)recovery_cs=std::max(recovery_cs,gap);}
  tft_cs=next;
}
static void set_dc(avr_irq_t*,uint32_t value,void*){dc=value&1;}
static void set_reset(avr_irq_t*,uint32_t value,void*) {
  if(trigger&&reset_level&&!(value&1)){
    ++reset_count;
    reset_low=avr->cycle;clear_pixels=paint_pixels=0;
    x1=y1=0;x2=239;y2=319;have_x=have_y=false;madctl=0;
  }
  reset_level=value&1;
}
static void set_permit(avr_irq_t*,uint32_t value,void*){if(trigger&&permit&&!(value&1))++permit_falls;permit=value&1;}
static void set_active(avr_irq_t*,uint32_t value,void*){if(trigger&&active&&!(value&1))++active_falls;active=value&1;}
static unsigned exchange(unsigned value) {
  unsigned answer=0;
  if(!can_cs) {
    if(!phase++) {command=value;if(command==0xc0)memset(regs,0,sizeof(regs));if(command==0x90||command==0x94)read_frame=command==0x94;}
    else if(command==0x90||command==0x94) {
      const auto& f=receive_buffers[read_frame];const unsigned at=phase-2;
      switch(at){case 0:answer=f.id>>21;break;case 1:answer=((f.id>>18)&7)<<5|8|((f.id>>16)&3);break;case 2:answer=f.id>>8;break;case 3:answer=f.id;break;case 4:answer=8;break;default:if(at<13)answer=f.data[at-5];}
    } else if(phase==2)address=value;
    else if(command==3)answer=regs[address++&127];
    else if(command==2)reg_write(address++&127,value);
    else if(command==5){if(phase==3)mask=value;else reg_write(address,(regs[address]&~mask)|(value&mask));}
  } else if(!tft_cs) {
    ++tft_bytes;
    if(!dc&&value==0xef){capture_init=1;init_hash=2166136261U;}
    if(capture_init){init_hash=(init_hash^value)*16777619U;if(!dc&&value==0x29){capture_init=0;if(reset_low)reset_hash=init_hash;else boot_hash=init_hash;}}
    if(!dc){tft_cmd=value;tft_parameter=0;if(value==0x2c){px=x1;py=y1;}}
    else if(tft_cmd==0xd9)tft_index=value;
    else if(tft_cmd==0xd3)answer=tft_index==0x12?0x93:0x41;
    else if(tft_cmd==0x36)madctl=value;
    else if(tft_cmd==0x2a||tft_cmd==0x2b){
      if(tft_parameter%2==0)window_byte=value;
      else {
        const unsigned v=window_byte*256+value;
        if(tft_cmd==0x2a){if(tft_parameter==1)x1=v;else{x2=v;have_x=true;}}
        else{if(tft_parameter==1)y1=v;else{y2=v;have_y=true;}}
      }
      ++tft_parameter;
    } else if(tft_cmd==0x2c&&reset_low) {
      if(++tft_parameter%2==0){
        if(clear_pixels<76800){
          if(!have_x||!have_y||madctl!=0xe8||px!=clear_pixels%320||py!=clear_pixels/320)++window_errors;
          ++clear_pixels;
        }else{++paint_pixels;if(!first_paint)first_paint=avr->cycle;last_paint=avr->cycle;}
        if(++px>x2){px=x1;if(++py>y2)py=y1;}
      }
    }
  }
  return answer&255;
}
static unsigned spi_reply;
static Cycle spi_done(avr_t* a,Cycle,void*) { a->data[0x4e]=spi_reply;a->data[0x4d]|=0x80;return 0; }
static void spi_write(avr_t* a,avr_io_addr_t at,uint8_t v,void*) {
  a->data[at]=v;a->data[0x4d]&=~0x80;spi_reply=exchange(v);
  static const unsigned div[]={4,16,64,128};unsigned divider=div[a->data[0x4c]&3];if(a->data[0x4d]&1)divider/=2;
  avr_cycle_timer_register(a,8*divider,spi_done,nullptr);
}
static uint8_t spi_read(avr_t* a,avr_io_addr_t at,void*){a->data[0x4d]&=~0x80;return a->data[at];}
static void serial_output(avr_irq_t*,uint32_t value,void*) {
  const char c=value&255;putchar(c);line+=c;
  if(c=='\n'){
    if(!boot&&line.find("Startup complete")!=std::string::npos)boot=avr->cycle;
    if(trigger&&line.find("last=ACK timeout")!=std::string::npos)timeout_reported=true;
    line.clear();
  }
}
static void external(char port,unsigned mask_,unsigned value) {
  avr_ioport_external_t e={};e.name=port;e.mask=mask_;e.value=value;avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL(port),&e);
  for(unsigned i=0;i<8;++i)if(mask_&(1U<<i))avr_raise_irq(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ(port),i),bool(value&(1U<<i)));
}
static void wheel_pins(bool button,bool clock=true,bool data=true) {
  // D5=PE3 button, D3=PE5 clock, D4=PG5 data. Preserve the live CAN INT level.
  external('E',0x38,(button?0:8)|(clock?32:0)|((regs[0x2b]&regs[0x2c]&3)?0:16));
  external('G',0x20,data?0x20:0);
}
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  mode=atoi(argv[3]);
  elf_firmware_t fw={};if(elf_read_firmware(argv[1],&fw)<0)return 3;
  avr=avr_make_mcu_by_name("atmega2560");avr_init(avr);avr->frequency=second;avr_load_firmware(avr,&fw);
  uint8_t eeprom[4096];FILE* ep=fopen(argv[2],"rb");if(!ep||fread(eeprom,1,sizeof(eeprom),ep)!=sizeof(eeprom))return 4;fclose(ep);
  avr_eeprom_desc_t desc={eeprom,0,sizeof(eeprom)};avr_ioctl(avr,AVR_IOCTL_EEPROM_SET,&desc);
  external('E',0x38,0x38);external('G',0x20,0x20);external('A',0x10,0); // ENABLE_IN ON, active low.
  irq_pin=avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('E'),4);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),4),select_can,nullptr);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),0),select_tft,nullptr);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),1),set_dc,nullptr);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),2),set_reset,nullptr);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('A'),5),set_permit,nullptr);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('C'),7),set_active,nullptr);
  avr->io[AVR_DATA_TO_IO(0x4e)].w.c=spi_write;avr->io[AVR_DATA_TO_IO(0x4e)].w.param=nullptr;
  avr->io[AVR_DATA_TO_IO(0x4e)].r.c=spi_read;avr->io[AVR_DATA_TO_IO(0x4e)].r.param=nullptr;
  uint32_t flags=0;avr_ioctl(avr,AVR_IOCTL_UART_SET_FLAGS('0'),&flags);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_OUTPUT),serial_output,nullptr);
  uart_in=avr_io_getirq(avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_INPUT);
  avr_int_vector_t* int4=nullptr;for(unsigned i=0;i<avr->interrupts.vector_count;++i)if(avr->interrupts.vector[i]->vector==5)int4=avr->interrupts.vector[i];
  avr_int_vector_t* timer1=nullptr;for(unsigned i=0;i<avr->interrupts.vector_count;++i)if(avr->interrupts.vector[i]->vector==20)timer1=avr->interrupts.vector[i];
  const auto tick=symbol(fw,"_ZN3psu11Application4tickEv"),receive=symbol(fw,"_ZN3psu16Mcp2515Transport7receiveERNS_8CanFrameE");
  const auto recovering=symbol(fw,"_ZNK3psu14Ili9341Display10recoveringEv");
  const auto drawing=symbol(fw,"_ZN3psu14Ili9341Display7serviceERKNS_7UiFrameE");
  const auto encoder=symbol(fw,"_ZN3psu12_GLOBAL__N_114serviceEncoderEv");
  // Verified from the linked AVR accessor: ldd r24,Z+2. Fail explicitly
  // if its layout changes instead of silently reading another object's state.
  if(recovering==~0U||fw.flash[recovering+4]!=0x82||fw.flash[recovering+5]!=0x81)return 7;
  if(!int4||!timer1||tick==~0U||receive==~0U)return 5;
  for(unsigned i=0;i<9;++i){voltage[i]=55296;broadcasts[i]=second*2+33000*mhz*i;}
  while(avr->cycle<second*90&&avr->state!=cpu_Crashed&&avr->state!=cpu_Done){
    const unsigned sp=avr->data[R_SPL]|unsigned(avr->data[R_SPH])<<8;
    if(avr->pc==encoder)++encoder_calls;
    if(boot){min_sp=std::min(min_sp,sp);if(trigger)reset_min_sp=std::min(reset_min_sp,sp);}
    if(avr->pc==recovering){
      const unsigned self=avr->data[24]|unsigned(avr->data[25])<<8;
      const unsigned state=avr->data[self+2];
      if(state&&!recovering_at)recovering_at=avr->cycle;
      if(state==7&&!clear_at)clear_at=avr->cycle;
      if(state==8&&!paint_at)paint_at=avr->cycle;
      if(!state&&reset_state&&!reset_end)reset_end=avr->cycle;
      reset_state=state;
    }
    if(mode>=7&&avr->pc==drawing){
      const unsigned frame=avr->data[22]|unsigned(avr->data[23])<<8;
      scene.assign(reinterpret_cast<const char*>(avr->data+frame),390);
      if(trigger&&mode==7&&scene.find("Configure global")!=std::string::npos)ordinary_hold_action=true;
      if(mode==8&&trigger&&avr->cycle>trigger+second*21&&scene.substr(26)==held_scene.substr(26))editor_preserved=true;
    }
    if(avr->pc==tick){if(last_loop&&stable){auto& v=trigger?max_loop_gap:before_loop;v=std::max(v,avr->cycle-last_loop);if(reset_state)recovery_loop=std::max(recovery_loop,avr->cycle-last_loop);}last_loop=avr->cycle;}
    if(avr->pc==receive){if(last_rx&&stable){auto& v=trigger?max_rx_gap:before_rx;v=std::max(v,avr->cycle-last_rx);if(reset_state)recovery_gap=std::max(recovery_gap,avr->cycle-last_rx);}last_rx=avr->cycle;}
    if(avr->cycle>=next_tick){next_tick=avr->cycle+100*mhz;
      for(unsigned i=0;i<9;++i)if(avr->cycle>=broadcasts[i]){broadcasts[i]+=300000*mhz;Frame f={};f.id=0x1000117eU|(i+1)<<16;f.data[1]=1;f.data[3]=standby[i]?1:0;const unsigned raw=standby[i]?0:target[i]*1000/1024;f.data[6]=raw>>8;f.data[7]=raw;queue(f,0);}
      if(!arrivals.empty()&&arrivals.top().at<=avr->cycle&&avr->cycle>=wire_free){wire_free=avr->cycle+1300*mhz;const auto f=arrivals.top().frame;arrivals.pop();
        unsigned n=!(regs[0x2c]&1)?0:!(regs[0x2c]&2)?1:2;
        if(n==2){++overflows;regs[0x2d]|=0xc0;}else{receive_buffers[n]=f;regs[0x2c]|=1U<<n;++received;if(reset_state)++during_received;irq_level();}
      }
      if(boot&&!stable&&std::all_of(target,target+9,[](unsigned a){return a>=204;})){stable=avr->cycle;printf("\nMODEL all-nine-at-target at=%.3f\n",double(stable)/second);}
      if(stable&&mode==8&&!trigger){
        // Real GPIO: ordinary hold/release -> global config -> current editor;
        // turn one detent to create an unapplied draft before the reset hold.
        static const unsigned at[]={3000,4400,5600,5750,6800,6950,8100,8140,8180,8220,9500};
        if(gpio_step<11&&avr->cycle>=stable+at[gpio_step]*1000*mhz){
          switch(gpio_step){
            case 0:case 2:case 4:wheel_pins(true);break;
            case 1:case 3:case 5:wheel_pins(false);break;
            case 6:wheel_pins(false,false,true);break;
            case 7:wheel_pins(false,false,false);break;
            case 8:wheel_pins(false,true,false);break;
            case 9:wheel_pins(false);break;
            case 10:
              held_scene=scene;
              if(scene.find("10.50")==std::string::npos&&scene.find("9.50")==std::string::npos){fprintf(stderr,"Editor setup failed: %s\nencoder-calls=%u TCCR1=%02x/%02x TIMSK1=%02x ICR1=%u CNT=%u PINE=%02x\n",scene.c_str(),encoder_calls,avr->data[0x80],avr->data[0x81],avr->data[0x6f],avr->data[0x86]|avr->data[0x87]<<8,avr->data[0x84]|avr->data[0x85]<<8,avr->data[0x2c]);return 16;}
              wheel_pins(true);trigger=avr->cycle;inject=1;break;
          }
          ++gpio_step;
        }
      }
      if(stable&&!inject&&mode!=8&&avr->cycle>=stable+second*3){inject=1;if(mode==4||mode==5)input+="set current all 11\napply all current\n";else{trigger=avr->cycle;if(mode==7)wheel_pins(true);else if(mode)input+="display reset\n";}}
      if(mode>=7&&trigger){
        const auto age=avr->cycle-trigger;
        if(age>=second*20+50000*mhz)wheel_pins(false);
        else if(age>=second*20+20000*mhz)wheel_pins(true);
        else if(age>=second*20)wheel_pins(false);
      }
      if(trigger&&!after_measure&&avr->cycle>trigger+second*(mode>=7?22:8)){after_measure=1;input+="diag bus\ndiag ui\ndiag memory\ndiag control\n";}
      if(reset_low&&!off_at&&(mode==2||mode==3)&&avr->cycle>=reset_low+50000*mhz){off_at=avr->cycle;if(mode==2)external('A',0x10,0x10);else queue(metric(0,9),0);}
      if(reset_low&&mode==6&&!save_sent&&avr->cycle>=reset_low+100000*mhz){input+="save\n";save_sent=1;}
      if(off_at&&!all_zero&&std::all_of(target,target+9,[](unsigned a){return a==0;}))all_zero=avr->cycle;
      if(!input.empty()&&avr->cycle>=next_uart){const auto c=input.front();avr_raise_irq(uart_in,c);input.erase(0,1);next_uart=avr->cycle+(c=='\n'?second:2000*mhz);}
    }
    if((regs[0x2b]&regs[0x2c]&3)&&(avr->data[0x3d]&0x10)&&!(avr->data[0x6a]&3))avr_raise_interrupt(avr,int4);
    if(avr->cycle>=next_encoder){
      next_encoder+=16000;
      if(avr->data[0x6f]&1){
        if(avr->data[0x80]!=0||avr->data[0x81]!=0x11||(avr->data[0x86]|avr->data[0x87]<<8)!=8000)return 18;
        avr_raise_interrupt(avr,timer1);
      }
    }
    avr_run(avr);
    if(trigger&&avr->cycle>=trigger+second*(mode>=7?30:17))break;
  }
  const auto heap=symbol(fw,"__heap_start")&0xffff;
  printf("\nSIM mode=%u boot=%.3f stable=%.3f trigger=%.3f reset-low=%.3f first-paint=%.3f last-paint=%.3f clear-pixels=%u paint-pixels=%u\n",mode,double(boot)/second,double(stable)/second,double(trigger)/second,double(reset_low)/second,double(first_paint)/second,double(last_paint)/second,clear_pixels,paint_pixels);
  printf("SIM rx=%u tx=%u hw-overflow=%u shared-cs=%u rx-gap-us before/after=%llu/%llu loop-gap-us=%llu/%llu tft-cs-us=%llu/%llu stack-room=%u reset-room=%u\n",received,transmitted,overflows,shared_cs,(unsigned long long)(before_rx/mhz),(unsigned long long)(max_rx_gap/mhz),(unsigned long long)(before_loop/mhz),(unsigned long long)(max_loop_gap/mhz),(unsigned long long)(before_cs/mhz),(unsigned long long)(max_cs/mhz),min_sp-heap+1,reset_min_sp-heap+1);
  printf("SIM recovery start=%.3f clear=%.3f paint=%.3f complete=%.3f rx-gap-us=%llu loop-gap-us=%llu tft-cs-us=%llu init-hash boot/reset=%08x/%08x frames-during=%u all-zero=%.3f\n",double(recovering_at)/second,double(clear_at)/second,double(paint_at)/second,double(reset_end)/second,(unsigned long long)(recovery_gap/mhz),(unsigned long long)(recovery_loop/mhz),(unsigned long long)(recovery_cs/mhz),boot_hash,reset_hash,during_received,double(all_zero)/second);
  printf("SIM zero-writes=%u standby-writes=%u permit-falls=%u active-falls=%u injected-stop-at=%.3f first-zero=%.3f last-zero=%.3f final-current-raw=",zero_after,standby_after,permit_falls,active_falls,double(off_at)/second,double(first_zero)/second,double(last_zero)/second);
  for(auto a:target)printf("%u,",a);
  puts("");
  avr_ioctl(avr,AVR_IOCTL_EEPROM_GET,&desc);
  const auto saved=std::string(argv[2])+".after-"+argv[3];
  FILE* out=fopen(saved.c_str(),"wb");if(!out)return 8;fwrite(eeprom,1,sizeof(eeprom),out);fclose(out);
  if(!boot||!stable||!trigger||overflows||shared_cs)return 9;
  if(mode&&(!reset_end||boot_hash!=reset_hash||clear_pixels!=76800||!paint_pixels||recovery_gap>=100000*mhz||window_errors))return 10;
  if((mode==0||mode==1||mode==4||mode==6)&&(zero_after||standby_after||permit_falls||active_falls))return 11;
  if((mode==2||mode==3)&&(!all_zero||first_zero-off_at>500000*mhz||active_falls!=1))return 12;
  if(mode==4&&(!delayed_ack||!std::all_of(target,target+9,[](unsigned a){return a==225;})))return 13;
  if(mode==5&&(!delayed_ack||!timeout_reported))return 14;
  if(mode>=7&&(reset_count!=1||zero_after||standby_after||permit_falls||active_falls||unwanted_current||ordinary_hold_action||recovering_at-trigger<10*second))return 15;
  if(mode==8&&!editor_preserved)return 17;
  if(mode>=7)printf("SIM wheel: resets=%u editor-preserved=%u unintended-action=%u\n",reset_count,editor_preserved,ordinary_hold_action);
  const auto milliseconds=avr->cycle/16000;
  if(encoder_calls+2000<milliseconds||encoder_calls>milliseconds+100)return 19;
  printf("SIM Timer1 ISR calls=%u\n",encoder_calls);
  puts("SIM PASS");return 0;
}
