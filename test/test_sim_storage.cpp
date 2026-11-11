#include "debug/Simulator.h"
#include "ui/Touch.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace psu;
int main() {
  static_assert(PSU_MAX_UNITS==4,"Exercise the AVR debug layout");
  RamStorage bytes; MemoryManager memory(bytes); auto cfg=debugConfiguration();
  assert(memory.save(cfg)==StorageResult::Ok);
  TouchCalibration transform; transform.valid=true;
  for(uint8_t i=0;i<6;++i) transform.coefficient[i]=i*12345;
  transform.coefficient[0]=transform.coefficient[4]=65536;
  CalibrationStore cal(bytes,memory); assert(cal.start(transform)==StorageResult::Ok); while(cal.saving()) cal.step();
  assert(cal.result()==StorageResult::Ok);
  for(uint8_t i=0;i<profiles::count;++i) {
    cfg.current[0]=100+i;
    assert(memory.startProfileSave(i,nullptr,cfg,0,true,5)==StorageResult::Ok);
    while(memory.saving()) memory.stepSave();
    assert(memory.saveResult()==StorageResult::Ok);
  }
  cfg.voltage[0]=5600; assert(memory.save(cfg)==StorageResult::Ok);
  Configuration loaded; assert(memory.load(loaded)==StorageResult::Ok && loaded.voltage[0]==5600);
  for(uint8_t i=0;i<profiles::count;++i) {
    ProfileInfo info; ProfileTarget target;
    assert(memory.profileInfo(i,info)==StorageResult::Ok && info.hasInclusion && info.included==5);
    assert(memory.profileTarget(i,0,info.generation,target)==StorageResult::Ok && target.current==100+i);
  }
  TouchCalibration restored; assert(cal.load(restored));
  for(uint8_t i=0;i<6;++i) assert(restored.coefficient[i]==transform.coefficient[i]);
  bytes.failWrites=true; cfg.voltage[0]=5700;
  assert(memory.save(cfg)==StorageResult::WriteFailed);
  bytes.failWrites=false;
  assert(memory.load(loaded)==StorageResult::Ok && loaded.voltage[0]==5600);
  assert(memory.startProtectionSave(ThermalStop::Temperature)==StorageResult::Ok);
  while(memory.saving()) memory.stepSave();
  assert(memory.persistedProtection(ThermalStop::Temperature));
  assert(cal.load(restored));
  std::cout << "PASS compact debug store: controller, eight profiles, calibration, failures and protection; no hardware storage\n";
}
