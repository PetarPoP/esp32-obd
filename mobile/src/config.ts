export const APP_CONFIG = {
  /**
   * Where vehicle profiles (profiles/<Make>/<Model>.json.gz from this repo)
   * are downloaded from. Point it at a fork or another branch if needed.
   */
  profileBaseUrl: "https://raw.githubusercontent.com/PetarPoP/esp32-obd/master/profiles/",
  /** Where firmware/version.json and the firmware image are downloaded from for OTA updates. */
  firmwareBaseUrl: "https://raw.githubusercontent.com/PetarPoP/esp32-obd/master/firmware/",
  /** Firmware limit for manufacturer signals (ExtPidService::kMaxSignals). */
  maxExtSignals: 96,
} as const;
