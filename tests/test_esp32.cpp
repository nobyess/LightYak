// SPDX-License-Identifier: MIT
#include "LightYak.h"
#include "Esp32Mock.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #c);}while(0)
using namespace lightyak;
using Bytes=std::vector<uint8_t>;
Bytes frame(uint8_t op,Bytes payload){
  Bytes body{1,op,0,17,static_cast<uint8_t>(payload.size())};body.insert(body.end(),payload.begin(),payload.end());
  uint16_t crc=0xffff;for(uint8_t b:body){crc^=uint16_t(b)<<8;for(int i=0;i<8;++i)crc=(crc&0x8000)?uint16_t((crc<<1)^0x1021):uint16_t(crc<<1);}
  body.push_back(uint8_t(crc));body.push_back(uint8_t(crc>>8));Bytes wire{0x7e};
  for(auto b:body){if(b==0x7e||b==0x7d){wire.push_back(0x7d);wire.push_back(b^0x20);}else wire.push_back(b);}wire.push_back(0x7e);return wire;
}
void feed(uint8_t op,Bytes p){auto b=frame(op,p);auto& s=esp32mock::state;s.input.insert(s.input.end(),b.begin(),b.end());}
Bytes response(){auto& s=esp32mock::state;Bytes b;bool escaped=false;for(size_t i=1;i+1<s.output.size();++i){uint8_t c=s.output[i];if(escaped){b.push_back(c^0x20);escaped=false;}else if(c==0x7d)escaped=true;else b.push_back(c);}CHECK(b.size()>=7);CHECK(b.size()==size_t(b[4]+7));return b;}
void pump(Device& d,int calls=300){auto& s=esp32mock::state;for(int i=0;i<calls;++i){int reads=s.readCalls,writes=s.fifoWrites;d.Update();auto w=d.LastWork();CHECK(w.received<=LIGHTYAK_RX_BUDGET);CHECK(w.transmitted<=LIGHTYAK_TX_BUDGET);CHECK(w.actions<=1);CHECK(w.tagCopies<=1);CHECK(s.readCalls-reads<=LIGHTYAK_RX_BUDGET);CHECK(s.fifoWrites-writes<=LIGHTYAK_TX_BUDGET);s.fifoSpace=128;}CHECK(s.waits==0);}
int main(){int passed=0,failed=0;auto test=[&](const char* name,void(*fn)()){try{esp32mock::reset();fn();++passed;std::cout<<"PASS "<<name<<"\n";}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";}};
  test("full-width Arduino serial configuration",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8E2,"ESP32"));auto& s=esp32mock::state;CHECK(s.baud==115200&&s.bits==UART_DATA_8_BITS&&s.parity==UART_PARITY_EVEN&&s.stop==UART_STOP_BITS_2);CHECK(s.rxPin==16&&s.txPin==17);CHECK(s.txSize==0&&s.eventQueueSize==0&&s.rxSize==256);CHECK(s.threshold==8&&s.timeout==2);});
  test("flash-name overload and idle RX never wait",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8N1,F("ESP32")));pump(d);CHECK(esp32mock::state.fifoWrites==0);});
  test("truncated or unsupported serial formats are rejected",[]{for(uint32_t config:{uint32_t(0x1c),uint32_t(0x08000018),uint32_t(0x0800001d),uint32_t(0x0800002c),uint32_t(0x8800001c)}){Esp32Uart p(1,16,17);CHECK(!p.Begin(115200,config));CHECK(p.LastError()==ESP_ERR_INVALID_ARG);}CHECK(esp32mock::state.installCalls==0);});
  test("invalid port pin baud and RX capacity are rejected",[]{Esp32Uart a(-1,16,17),b(3,16,17),c(1,-1,17),d(1,16,16),e(1,16,17,128),f(1,16,17);CHECK(!a.Begin(115200,SERIAL_8N1));CHECK(!b.Begin(115200,SERIAL_8N1));CHECK(!c.Begin(115200,SERIAL_8N1));CHECK(!d.Begin(115200,SERIAL_8N1));CHECK(!e.Begin(115200,SERIAL_8N1));CHECK(!f.Begin(0,SERIAL_8N1));CHECK(esp32mock::state.installCalls==0);});
  test("existing drivers are never stolen or deleted",[]{esp32mock::state.installed[1]=true;{Esp32Uart p(1,16,17);Device d;CHECK(!d.Listen(p,115200,SERIAL_8N1,"ESP32"));CHECK(d.LastError()==NoTransport);CHECK(p.LastError()==ESP_ERR_INVALID_STATE);}CHECK(esp32mock::state.installed[1]);CHECK(esp32mock::state.deleteCalls==0);});
  test("setup failures release only the newly installed driver",[]{for(int stage=1;stage<=5;++stage){esp32mock::state.failStage=stage;Esp32Uart p(1,16,17);CHECK(!p.Begin(115200,SERIAL_8N1));CHECK(!p.Ready());CHECK(!esp32mock::state.installed[1]);CHECK(!p.Adapter().read);}CHECK(esp32mock::state.deleteCalls==4);});
  test("End is idempotent and permits explicit restart",[]{Esp32Uart p(1,16,17);CHECK(p.Begin(115200,SERIAL_8N1));CHECK(!p.Begin(115200,SERIAL_8N1));p.End();p.End();CHECK(esp32mock::state.deleteCalls==1);CHECK(p.Begin(115200,SERIAL_8N1));CHECK(p.Ready());});
  test("IDF UART is allocated only during setup",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));feed(Identify,{});pump(d);CHECK(esp32mock::state.installCalls==1);CHECK(esp32mock::state.configCalls==1);CHECK(response()[1]==0x81);});
  test("full FIFO defers output without consuming bytes",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));feed(Identify,{});auto& s=esp32mock::state;for(int i=0;i<300;++i){s.fifoSpace=0;d.Update();CHECK(d.LastWork().transmitted==0);}CHECK(s.output.empty());pump(d);CHECK(response()[1]==0x81);});
  test("one free FIFO slot never permits a second write",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));feed(Identify,{});auto& s=esp32mock::state;for(int i=0;i<300;++i){s.fifoSpace=1;int before=s.fifoWrites;d.Update();CHECK(s.fifoWrites-before<=1);}CHECK(response()[1]==0x81);});
  test("contended tag reads defer and never expose an internal status",[]{Esp32Uart p(1,16,17);Device d;uint32_t value=0x12345678;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));CHECK(d.AddTag(value,"Value",ReadWrite)==1);feed(Read,{1,1,0,0});auto& s=esp32mock::state;s.tagBusy=true;pump(d);CHECK(s.output.empty());CHECK(s.tryLocks>0&&s.unlocks==0);s.tagBusy=false;pump(d);auto b=response();CHECK(b[1]==0x83&&b[9]==Ok&&b[12]==0x78);CHECK(s.unlocks==1);});
  test("contended writes preserve the value until a later call",[]{Esp32Uart p(1,16,17);Device d;uint16_t value=1200;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));d.AddTag(value,"Value",ReadWrite);feed(Write,{1,1,0,0,UInt16,2,42,0});auto& s=esp32mock::state;s.tagBusy=true;pump(d);CHECK(value==1200&&s.output.empty());s.tagBusy=false;pump(d);CHECK(value==42);auto b=response();CHECK(b[1]==0x84&&b[9]==Ok);CHECK(s.unlocks==1);});
  test("deferred later batch item never reapplies an earlier write",[]{Esp32Uart p(1,16,17);Device d;uint16_t first=0,second=0;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));d.AddTag(first,"First",ReadWrite);d.AddTag(second,"Second",ReadWrite);feed(Write,{2,1,0,0,UInt16,2,42,0,2,0,0,UInt16,2,77,0});auto& s=esp32mock::state;for(int i=0;i<300&&first!=42;++i)d.Update();CHECK(first==42&&second==0);s.tagBusy=true;first=99;pump(d);CHECK(first==99&&second==0&&s.output.empty());s.tagBusy=false;pump(d);CHECK(first==99&&second==77);CHECK(response()[1]==0x84);});
  test("public try-lock supports a cooperating value producer",[]{Device d;esp32mock::state.tagBusy=true;CHECK(!d.TryLockTags());esp32mock::state.tagBusy=false;CHECK(d.TryLockTags());d.UnlockTags();CHECK(d.TryLockTags());d.UnlockTags();CHECK(esp32mock::state.waits==0);});
  test("device properties remain readable during value contention",[]{Esp32Uart p(1,16,17);Device d;CHECK(d.Listen(p,115200,SERIAL_8N1,"ESP32"));esp32mock::state.tagBusy=true;esp32mock::state.now=500;feed(Read,{1,0,0,Heartbeat});pump(d);auto b=response();CHECK(b[1]==0x83&&b[12]==1);CHECK(esp32mock::state.tryLocks==0);});
  std::cout<<passed<<" ESP32 adapter tests passed, "<<failed<<" failed\n";return failed?1:0;
}
