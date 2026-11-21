# Brim prototype: phone app + desktop camera

Dev only. None of this is in the build that goes on the hat (`npm run build` is unchanged).

```
cd app
npm ci
npm run camera      # opens the desktop camera page; also serves the phone page on your Wi-Fi
```

## Desktop camera: `http://localhost:5173/#/camera`

The laptop webcam plays the part of the hat. Click **Start camera**.

- Middle: the hat's view with boxes (id, label, confidence, distance, "approaching") and the
  walking corridor with left / ahead / right.
- Right: what Brim says, the path, what's in view, timing, distance calibration, recent phrases.
- Sidebar: QR code to link a phone. Once a phone is linked, the phone speaks and the laptop is quiet.

All of the logic is in `src/vision/useBrimCamera.ts`, so another layout only needs the hook:

| File | What it does |
|---|---|
| `vision/useBrimCamera.ts` | webcam → detector → tracker → nav → guide → phones, as one hook |
| `vision/detector.ts` | EfficientDet-Lite0 in the browser (MediaPipe), same model family as the hat |
| `vision/nav.ts` | line-by-line port of `device/src/nav/nav.cpp` (tests mirror `test_nav.cpp`) |
| `vision/tracker.ts` | anonymous tracks, distance from box height, "approaching" from box growth |
| `vision/guide.ts` | walking words ("Chair ahead, 1 metre. Step right."), sensitivity, hazards on/off |
| `vision/overlay.ts` | the security-camera drawing |
| `vision/link.ts` + `vite.config.ts` | laptop ⇄ phone relay over the local Wi-Fi (`/camlink`) |

## Phone: scan the QR code, or open `http://<laptop-ip>:5173/#/phone`

The Figma screens, working:

1. **Onboarding** (O1–O4): Brim says hi, type any 6-digit code (or "Use the demo"), play a test
   phrase, start walking. Tapping a button also turns on the phone's voice.
2. **Live** (D1–D5): what Brim just said, the details as plain rows, Brim the bunny, the voice bar.
   - **Demo walk**: plays the D1–D5 story (someone following → safe place → guided → safe).
   - **Laptop camera**: switches on by itself as soon as the laptop camera sends something.
   - **Voice bar**: tap and speak, where the browser supports it (Chrome, Safari): "What's around
     me?", "Repeat", "Settings", "Call Mom", "I'm okay". Without speech recognition a tap repeats
     the last instruction. Calls are pretend: nothing is dialled.
3. **Settings** (gear, top right):
   - The scale: **Detect only** (shows, never speaks) → **Warn** (speaks warnings only) →
     **Warn and guide** (warnings and directions).
   - **Sensitivity** and **Hazards and accidents** are sent to the laptop camera and change when
     it warns. **Voice** changes the speaking speed. **Warn me after**, **Share my location** and
     **Save a description** change the demo walk's words.
   - **Prototype**: pick Demo walk or Laptop camera, set the trusted contact's name, show
     onboarding again.

Settings are saved on the phone only (localStorage).

If the phone can't connect: hackathon Wi-Fi often blocks devices from talking to each other.
Turn on your phone's hotspot, join it from the laptop, and scan the new QR code.
