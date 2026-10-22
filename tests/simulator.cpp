// SPDX-License-Identifier: MIT
// Native device bridge for cross-language tests. Each line: <millis> <wire hex>.
#include "LightYak.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
struct Link {
  std::vector<uint8_t> in,out;size_t pos=0;uint32_t now=0;
  static int read(void* p){Link& l=*static_cast<Link*>(p);return l.pos<l.in.size()?l.in[l.pos++]:-1;}
  static bool write(void* p,uint8_t b){static_cast<Link*>(p)->out.push_back(b);return true;}
  static uint32_t clock(void* p){return static_cast<Link*>(p)->now;}
};
int main(){
  Link l;lightyak::Device d;lightyak::Transport t={&l,Link::read,Link::write,Link::clock};
  bool enabled=false;int16_t rpm=1200;float temp=22.5f;
  if(!d.Listen(t,"Simulated Uno"))return 1;
  d.AddTag(enabled,"Enabled",lightyak::ReadWrite);
  d.AddTag(rpm,"RPM",lightyak::ReadWrite);
  d.AddTag(temp,"Temperature");
  std::string line;
  while(std::getline(std::cin,line)){
    std::istringstream stream(line);std::string hex;stream>>l.now>>hex;
    l.in.clear();l.out.clear();l.pos=0;
    try{for(size_t i=0;i+1<hex.size();i+=2)l.in.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));}catch(...){return 2;}
    for(int i=0;i<2000;++i)d.Update();
    for(uint8_t b:l.out)std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);
    std::cout<<std::dec<<"\n"<<std::flush;
  }
}
