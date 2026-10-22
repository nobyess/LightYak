// SPDX-License-Identifier: MIT
// Native ESP32 simulator using the actual UART adapter with fake IDF/HAL calls.
#include "LightYak.h"
#include "Esp32Mock.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
int main(){
  lightyak::Esp32Uart port(1,16,17);lightyak::Device device;
  bool enabled=false;int16_t rpm=1200;float temp=22.5f;
  if(!device.Listen(port,115200,SERIAL_8N1,"Simulated Uno"))return 1;
  device.AddTag(enabled,"Enabled",lightyak::ReadWrite);
  device.AddTag(rpm,"RPM",lightyak::ReadWrite);
  device.AddTag(temp,"Temperature");
  std::string line;auto& s=esp32mock::state;
  while(std::getline(std::cin,line)){
    std::istringstream stream(line);std::string hex;stream>>s.now>>hex;
    s.input.clear();s.output.clear();s.inputPosition=0;
    try{for(size_t i=0;i+1<hex.size();i+=2)s.input.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));}catch(...){return 2;}
    for(int i=0;i<2000;++i){s.fifoSpace=128;device.Update();}
    for(uint8_t b:s.output)std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);
    std::cout<<std::dec<<"\n"<<std::flush;
  }
}
