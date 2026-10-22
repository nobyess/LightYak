// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_ESP32_MOCK_H
#define LIGHTYAK_ESP32_MOCK_H
#include <stdint.h>
#include <stddef.h>
#include <vector>
namespace esp32mock {
struct State {
  bool installed[3] = {false,false,false};
  uint32_t now = 0;
  int installCalls = 0, deleteCalls = 0, configCalls = 0, readCalls = 0;
  int fifoChecks = 0, fifoWrites = 0, tryLocks = 0, unlocks = 0;
  int waits = 0, failStage = 0, lastPort = -1, rxSize = 0, txSize = -1;
  int eventQueueSize = -1, rxPin = -1, txPin = -1, threshold = 0, timeout = 0;
  int baud = 0, bits = 0, parity = 0, stop = 0;
  bool tagBusy = false;
  unsigned fifoSpace = 128;
  size_t inputPosition = 0;
  std::vector<uint8_t> input, output;
  State() { input.reserve(65536); output.reserve(1048576); }
};
extern State state;
void reset();
bool tryLock(bool* held, int cycles);
void unlock(bool* held);
}
#endif
