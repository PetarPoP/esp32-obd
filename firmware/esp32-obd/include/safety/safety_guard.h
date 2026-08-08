#pragma once

namespace obd {
namespace safety {

class SafetyGuard {
 public:
  bool IsReadOnlyEnabled() const;
  bool IsClearDtcAllowed() const;

 private:
  static constexpr bool kReadOnlyDefault = true;
  static constexpr bool kAllowClearDtcDefault = false;
};

}  // namespace safety
}  // namespace obd
