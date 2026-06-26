# Glimmer Android App

This is the formal Android product app workspace for Glimmer. It is separate from
`android_glimmer_probe/`, which remains the low-level BLE validation tool.

Current scope:

- independent Android application id: `com.glimmer.app`
- product-facing app label: `微光`
- native Java Activity skeleton
- startup state, permission request entry, and four MVP tabs
- no B85 hardware dependency yet

Build:

```powershell
cd android_glimmer_app
.\gradlew.bat assembleDebug
```

APK output:

```text
android_glimmer_app\app\build\outputs\apk\debug\app-debug.apk
```

Next implementation step:

- extract the BLE connection layer from `android_glimmer_probe`
- connect the Nearby page to Glimmer device scan/connect state
- keep the probe app available for firmware/debug validation
