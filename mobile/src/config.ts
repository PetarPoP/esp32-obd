export const APP_CONFIG = {
  /**
   * Where vehicle profiles (profiles/<Make>/<Model>.json.gz from this repo)
   * are downloaded from. Point it at a fork or another branch if needed.
   */
  profileBaseUrl: "https://raw.githubusercontent.com/PetarPoP/esp32-obd/master/profiles/",
  /** Firmware limit for manufacturer signals (ExtPidService::kMaxSignals). */
  maxExtSignals: 96,
} as const;
