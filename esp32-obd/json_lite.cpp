#include "json_lite.h"

namespace obd {
namespace util {

JsonObject::JsonObject(const String& text) : text_(text) {
  int pos = SkipSpace(0);
  const int len = static_cast<int>(text_.length());
  if (pos >= len || text_.charAt(pos) != '{') {
    return;
  }
  ++pos;
  while (true) {
    pos = SkipSpace(pos);
    if (pos >= len) {
      return;
    }
    if (text_.charAt(pos) == '}') {
      valid_ = true;
      return;
    }
    if (text_.charAt(pos) != '"') {
      return;
    }
    const int key_end = SkipString(pos);
    if (key_end < 0) {
      return;
    }
    Field field;
    field.key_start = pos + 1;
    field.key_end = key_end - 1;
    pos = SkipSpace(key_end);
    if (pos >= len || text_.charAt(pos) != ':') {
      return;
    }
    pos = SkipSpace(pos + 1);
    const int value_end = SkipValue(pos);
    if (value_end < 0) {
      return;
    }
    field.start = pos;
    field.end = value_end;
    if (field_count_ < kMaxFields) {
      fields_[field_count_++] = field;
    }
    pos = SkipSpace(value_end);
    if (pos < len && text_.charAt(pos) == ',') {
      ++pos;
    }
  }
}

int JsonObject::SkipSpace(int pos) const {
  const int len = static_cast<int>(text_.length());
  while (pos < len) {
    const char c = text_.charAt(pos);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      break;
    }
    ++pos;
  }
  return pos;
}

int JsonObject::SkipString(int pos) const {
  const int len = static_cast<int>(text_.length());
  for (int i = pos + 1; i < len; ++i) {
    const char c = text_.charAt(i);
    if (c == '\\') {
      ++i;
    } else if (c == '"') {
      return i + 1;
    }
  }
  return -1;
}

int JsonObject::SkipValue(int pos) const {
  const int len = static_cast<int>(text_.length());
  if (pos >= len) {
    return -1;
  }
  const char c = text_.charAt(pos);
  if (c == '"') {
    return SkipString(pos);
  }
  if (c == '{' || c == '[') {
    int depth = 0;
    for (int i = pos; i < len; ++i) {
      const char d = text_.charAt(i);
      if (d == '"') {
        i = SkipString(i);
        if (i < 0) {
          return -1;
        }
        --i;
      } else if (d == '{' || d == '[') {
        ++depth;
      } else if (d == '}' || d == ']') {
        if (--depth == 0) {
          return i + 1;
        }
      }
    }
    return -1;
  }
  // number, true, false, null
  int i = pos;
  while (i < len) {
    const char d = text_.charAt(i);
    if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' || d == '\r' || d == '\n') {
      break;
    }
    ++i;
  }
  return i > pos ? i : -1;
}

const JsonObject::Field* JsonObject::Find(const char* key) const {
  const int key_len = static_cast<int>(strlen(key));
  for (int i = 0; i < field_count_; ++i) {
    const Field& f = fields_[i];
    if (f.key_end - f.key_start == key_len &&
        strncmp(text_.c_str() + f.key_start, key, key_len) == 0) {
      return &f;
    }
  }
  return nullptr;
}

String JsonObject::DecodeScalar(int start, int end) const {
  if (text_.charAt(start) != '"') {
    return text_.substring(start, end);
  }
  String out;
  for (int i = start + 1; i < end - 1; ++i) {
    char c = text_.charAt(i);
    if (c == '\\' && i + 1 < end - 1) {
      c = text_.charAt(++i);
      switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u':  // keep ASCII only; skip the four hex digits
          i += 4;
          continue;
        default: break;
      }
    }
    out += c;
  }
  return out;
}

bool JsonObject::Has(const char* key) const { return Find(key) != nullptr; }

String JsonObject::GetString(const char* key, const String& fallback) const {
  const Field* f = Find(key);
  if (f == nullptr || text_.charAt(f->start) == '{' || text_.charAt(f->start) == '[') {
    return fallback;
  }
  return DecodeScalar(f->start, f->end);
}

double JsonObject::GetNumber(const char* key, double fallback) const {
  const Field* f = Find(key);
  if (f == nullptr) {
    return fallback;
  }
  String s = DecodeScalar(f->start, f->end);
  s.trim();
  if (s.length() == 0) {
    return fallback;
  }
  char* end = nullptr;
  const double v = strtod(s.c_str(), &end);
  return (end == s.c_str()) ? fallback : v;
}

bool JsonObject::GetBool(const char* key, bool fallback) const {
  const Field* f = Find(key);
  if (f == nullptr) {
    return fallback;
  }
  const String s = DecodeScalar(f->start, f->end);
  if (s == "true" || s == "1") {
    return true;
  }
  if (s == "false" || s == "0") {
    return false;
  }
  return fallback;
}

}  // namespace util
}  // namespace obd
