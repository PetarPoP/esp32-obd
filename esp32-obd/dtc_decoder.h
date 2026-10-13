#pragma once

#include <Arduino.h>

namespace obd {
namespace core {

class DtcDecoder {
 public:
  String DecodeOrUnknown(const String& raw_code) const;

  // Formats the two raw DTC bytes as SAE J2012 text, e.g. 01 30 -> "P0130".
  // Returns an empty string for the 00 00 filler pair.
  static String Format(uint8_t high, uint8_t low);
};

}  // namespace core
}  // namespace obd
