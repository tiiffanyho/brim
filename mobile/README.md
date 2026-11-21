# Brim iPhone app

Expo / React Native, TypeScript. Speaks the hat's warnings and directions with the phone's own
voice, also with the phone locked.

## Run it on your iPhone

```
cd mobile
npm ci
npx expo start
```

Scan the QR code with the iPhone camera (Expo Go installed, same Wi-Fi as the laptop).

## What's on screen (Figma "Brim hat")

- **Onboarding** (first run): hello → connect → hear Brim → start walking.
  - *Connect* pairs with a hat (address + 6-digit PIN, same as before), **or** uses the
    **laptop camera** instead: on the laptop run `cd app && npm run camera`, click *Start camera*,
    and type the address it shows under *Link a phone* (e.g. `192.168.1.20`).
- **Live**: what Brim just said, what it sees (obstacle, people, path), the next step, and Brim.
  The glass bar says the last thing again. Settings are behind the button in the top right, with a
  red count for new safety alerts.
- **Settings**: *How much Brim does* (Detect only → Warn → Warn and guide), *Use Brim for*,
  *Sensitivity*, *Warn me after*, *When it happens*, *Voice*. Hat settings save to the hat as you
  change them (with the laptop camera they're kept on the phone and sent to the camera).
  *Alerts*, *Activity* and *Hat health* open from here.

## How it fits together

| File | What it does |
|---|---|
| `App.tsx` | onboarding first, then Live; Settings and its pages (no navigation library) |
| `src/BrimContext.tsx` | pairing, live events, speech queue, health; plus laptop camera, level, vibration |
| `src/core/` | pure logic with tests: phrases, speech order, reconnecting stream, camera address, level |
| `src/camera.ts` | the laptop camera as a "hat": settings on the phone, sent to the camera |
| `src/design.tsx`, `src/theme.ts` | the redesign's pieces (glass voice bar, flat toggles, scale, rows) |
| `assets/brim/` | the bunnies and the three glow backgrounds, pre-rendered (no drawing library needed) |

No new dependencies: the design uses React Native's own components and the Expo modules the app
already had, so Expo Go and the native build are unchanged.

Checks: `npx tsc --noEmit` and `npm test`.
