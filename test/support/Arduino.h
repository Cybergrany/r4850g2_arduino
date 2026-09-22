#pragma once
// Minimal host substitute for Arduino Print/Stream, used only by console tests.
#include <stddef.h>
#include <stdint.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

class __FlashStringHelper;
#define F(value) reinterpret_cast<const __FlashStringHelper*>(value)
#define HEX 16
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) write(bytes[i]);
    return length;
  }
  void print(const char* s) { while (*s) write(uint8_t(*s++)); }
  void print(const __FlashStringHelper* s) { print(reinterpret_cast<const char*>(s)); }
  void print(char c) { write(uint8_t(c)); }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  void print(T value, int base = 10) {
    std::ostringstream s;
    if (base == HEX) s << std::hex << std::uppercase;
    s << static_cast<long long>(value); print(s.str().c_str());
  }
  void print(double value, int decimals = 2) {
    std::ostringstream s; s << std::fixed << std::setprecision(decimals) << value;
    print(s.str().c_str());
  }
  void println() { print("\r\n"); }
  template <typename T> void println(T value) { print(value); println(); }
  template <typename T> void println(T value, int precision) { print(value, precision); println(); }
};
class Stream : public Print {
 public:
  virtual int availableForWrite() { return 64; }
  virtual int available() = 0;
  virtual int read() = 0;
};
