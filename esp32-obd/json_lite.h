#pragma once

#include <Arduino.h>

namespace obd {
namespace util {

// Minimal reader for the flat JSON command objects the app sends, e.g.
//   {"cmd":"start_stream","metrics":["rpm","speed_kmh"],"limit":5}
// Top-level keys are indexed once; nested objects and arrays are skipped
// correctly but only arrays of strings/numbers can be read back.
class JsonObject {
 public:
  explicit JsonObject(const String& text);

  bool IsValid() const { return valid_; }
  bool Has(const char* key) const;
  String GetString(const char* key, const String& fallback = String()) const;
  double GetNumber(const char* key, double fallback) const;
  bool GetBool(const char* key, bool fallback) const;

  // Calls fn(value) for every string (or number, as text) in an array field.
  // Returns the number of elements visited.
  template <typename Fn>
  int ForEachArrayItem(const char* key, Fn fn) const {
    const Field* field = Find(key);
    if (field == nullptr || text_.charAt(field->start) != '[') {
      return 0;
    }
    int count = 0;
    int pos = field->start + 1;
    while (true) {
      pos = SkipSpace(pos);
      if (pos >= field->end || text_.charAt(pos) == ']') {
        return count;
      }
      const int end = SkipValue(pos);
      if (end < 0) {
        return count;
      }
      fn(DecodeScalar(pos, end));
      ++count;
      pos = SkipSpace(end);
      if (pos < static_cast<int>(text_.length()) && text_.charAt(pos) == ',') {
        ++pos;
      }
    }
  }

 private:
  struct Field {
    int key_start;
    int key_end;  // exclusive, raw key without quotes
    int start;    // value span
    int end;
  };
  static constexpr int kMaxFields = 40;

  const Field* Find(const char* key) const;
  int SkipSpace(int pos) const;
  int SkipString(int pos) const;  // pos at opening quote; returns index after closing quote
  int SkipValue(int pos) const;
  String DecodeScalar(int start, int end) const;

  const String& text_;
  Field fields_[kMaxFields];
  int field_count_ = 0;
  bool valid_ = false;
};

}  // namespace util
}  // namespace obd
