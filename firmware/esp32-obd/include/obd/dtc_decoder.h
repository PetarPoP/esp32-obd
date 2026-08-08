#pragma once

#include <Arduino.h>

namespace obd {
namespace core {

class DtcDecoder {
 public:
  String DecodeOrUnknown(const String& raw_code) const;
};

}  // namespace core
}  // namespace obd
