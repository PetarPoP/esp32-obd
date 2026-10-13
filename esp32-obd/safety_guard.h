#pragma once

namespace obd {
namespace safety {

class SafetyGuard {
 public:
  bool IsReadOnlyEnabled() const;
  bool IsClearDtcAllowed() const;

 private:
  static constexpr bool kReadOnlyDefault = true;
  // Clearing DTCs is allowed only through DiagnosticsService::ClearDtc, which
  // requires an app confirmation token, a stationary vehicle and the engine
  // off. Set to false to lock it completely.
  static constexpr bool kAllowClearDtcDefault = true;
};

}  // namespace safety
}  // namespace obd
