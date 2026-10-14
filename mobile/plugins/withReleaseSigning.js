// Expo config plugin: signs `assembleRelease` with your own keystore when the
// ANDROID_KEYSTORE_* environment variables are set (GitHub Actions secrets),
// otherwise with the debug key as before. ANDROID_VERSION_CODE, when set,
// overrides versionCode so every release build installs over the previous one.
const { withAppBuildGradle } = require("expo/config-plugins");

const RELEASE_SIGNING = `
        release {
            if (System.getenv("ANDROID_KEYSTORE_PATH")) {
                storeFile file(System.getenv("ANDROID_KEYSTORE_PATH"))
                storePassword System.getenv("ANDROID_KEYSTORE_PASSWORD")
                keyAlias System.getenv("ANDROID_KEY_ALIAS")
                keyPassword System.getenv("ANDROID_KEY_PASSWORD")
            }
        }`;

module.exports = function withReleaseSigning(config) {
  return withAppBuildGradle(config, (cfg) => {
    let gradle = cfg.modResults.contents;
    if (!gradle.includes("ANDROID_KEYSTORE_PATH")) {
      gradle = gradle.replace(/signingConfigs\s*\{/, (m) => m + RELEASE_SIGNING);
      gradle = gradle.replace(
        /(buildTypes\s*\{[\s\S]*?release\s*\{[\s\S]*?)signingConfig signingConfigs\.debug/,
        '$1signingConfig System.getenv("ANDROID_KEYSTORE_PATH") ? signingConfigs.release : signingConfigs.debug'
      );
      gradle = gradle.replace(
        /versionCode\s+\d+/,
        (m) => `versionCode(System.getenv("ANDROID_VERSION_CODE") ? System.getenv("ANDROID_VERSION_CODE").toInteger() : ${m.split(/\s+/)[1]})`
      );
    }
    cfg.modResults.contents = gradle;
    return cfg;
  });
};
