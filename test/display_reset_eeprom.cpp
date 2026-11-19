// Host-only fixture: create the boot journal with the production writer.
#include "storage/MemoryManager.h"
#include <array>
#include <cstdio>
#include <cstring>
struct Bytes : psu::ByteStorage {
  std::array<uint8_t,4096> data;
  Bytes() { data.fill(0xff); }
  uint16_t length() const override { return data.size(); }
  uint8_t read(uint16_t at) const override { return data.at(at); }
  void update(uint16_t at,uint8_t value) override { data.at(at)=value; }
};
int main(int argc,char** argv) {
  if(argc!=2&&argc!=3) return 2;
  Bytes bytes; psu::MemoryManager memory(bytes);
  if(argc==3) {
    FILE* in=fopen(argv[1],"rb");if(!in)return 6;
    const auto n=fread(bytes.data.data(),1,bytes.data.size(),in);fclose(in);
    if(n!=bytes.data.size())return 7;
    psu::Configuration loaded;
    const auto result=memory.load(loaded);
    if(result!=psu::StorageResult::Ok)return 8;
    printf("EEPROM verified stop=%u autoresume=%u count=%u authorized-current=%u\n",unsigned(loaded.thermalStop),unsigned(loaded.autoResume),unsigned(loaded.count),loaded.operating.current[0]);
    return 0;
  }
  auto c=psu::defaultConfiguration(); c.count=9; c.autoResume=true;
  memset(c.groups,0,sizeof(c.groups));
  for(unsigned g=0;g<3;++g) {
    snprintf(c.groups[g].name,sizeof(c.groups[g].name),"GROUP%u",g+1);
    c.groups[g].members=7U<<(3*g);
  }
  for(unsigned i=0;i<9;++i) {
    c.units[i].identity={{0x53,0x49,0x4d,0,0,uint8_t(i+1)}};
    c.units[i].ratedCurrent=5000;
    c.voltage[i]=c.operating.voltage[i]=5400;
    c.current[i]=c.operating.current[i]=1000;
  }
  c.operating.voltageMask=c.operating.currentMask=511;
  if(memory.save(c)!=psu::StorageResult::Ok) return 3;
  FILE* out=fopen(argv[1],"wb"); if(!out) return 4;
  const auto size=fwrite(bytes.data.data(),1,bytes.data.size(),out); fclose(out);
  return size==bytes.data.size()?0:5;
}
