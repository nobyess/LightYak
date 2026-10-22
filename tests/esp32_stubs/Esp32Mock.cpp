// SPDX-License-Identifier: MIT
#include "Esp32Mock.h"
#include "Arduino.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include <stdexcept>
namespace esp32mock {
State state;
void reset() { state=State(); }
bool tryLock(bool* held,int cycles) {
  ++state.tryLocks;if(cycles!=0)++state.waits;
  if(state.tagBusy||*held)return false;
  *held=true;return true;
}
void unlock(bool* held) {if(!*held)throw std::runtime_error("unlock without lock");*held=false;++state.unlocks;}
}
HardwareSerial Serial;
uart_dev_t mock_uart_devices[3]={{0},{1},{2}};
bool uart_is_driver_installed(uart_port_t p){return esp32mock::state.installed[p];}
esp_err_t uart_driver_install(uart_port_t p,int rx,int tx,int queue,void*,int){
  auto& s=esp32mock::state;++s.installCalls;s.lastPort=p;s.rxSize=rx;s.txSize=tx;s.eventQueueSize=queue;
  if(s.failStage==1)return ESP_FAIL;
  s.installed[p]=true;return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t p){auto& s=esp32mock::state;++s.deleteCalls;s.installed[p]=false;return ESP_OK;}
esp_err_t uart_param_config(uart_port_t,const uart_config_t* c){auto& s=esp32mock::state;++s.configCalls;s.baud=c->baud_rate;s.bits=c->data_bits;s.parity=c->parity;s.stop=c->stop_bits;return s.failStage==2?ESP_FAIL:ESP_OK;}
esp_err_t uart_set_pin(uart_port_t,int tx,int rx,int,int){auto& s=esp32mock::state;s.rxPin=rx;s.txPin=tx;return s.failStage==3?ESP_FAIL:ESP_OK;}
esp_err_t uart_set_rx_full_threshold(uart_port_t,int n){auto& s=esp32mock::state;s.threshold=n;return s.failStage==4?ESP_FAIL:ESP_OK;}
esp_err_t uart_set_rx_timeout(uart_port_t,int n){auto& s=esp32mock::state;s.timeout=n;return s.failStage==5?ESP_FAIL:ESP_OK;}
int uart_read_bytes(uart_port_t,void* b,uint32_t n,TickType_t wait){
  auto& s=esp32mock::state;++s.readCalls;
  if(wait!=0||n!=1){++s.waits;throw std::runtime_error("nonzero RX wait or unbounded read");}
  if(s.inputPosition==s.input.size())return 0;
  *static_cast<uint8_t*>(b)=s.input[s.inputPosition++];return 1;
}
uint32_t uart_ll_get_txfifo_len(uart_dev_t* dev){auto& s=esp32mock::state;++s.fifoChecks;s.lastPort=dev->number;return s.fifoSpace;}
void uart_ll_write_txfifo(uart_dev_t*,const uint8_t* b,uint32_t n){
  auto& s=esp32mock::state;
  if(n!=1||!s.fifoSpace)throw std::runtime_error("FIFO write without space");
  ++s.fifoWrites;--s.fifoSpace;s.output.push_back(*b);
}
