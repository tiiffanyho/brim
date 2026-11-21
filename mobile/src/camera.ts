// Laptop camera instead of a hat (until the hat is ready, and for demos): the web app's camera page
// (app/, `npm run camera`) relays the hat's own events to phones over the local Wi-Fi at
// ws://<laptop>:5173/camlink?role=phone. Same events as the hat, so the rest of the app is unchanged.
// The laptop has no REST API, so settings live on this phone and are sent to the camera.
import AsyncStorage from "@react-native-async-storage/async-storage";
import type { HealthResponse, Settings } from "@schema";
import { ApiError, type HatApi } from "./api";
import { cameraWsUrl } from "./core/source";

export { normalizeCameraUrl } from "./core/source";

const SETTINGS_KEY = "brim.cameraSettings";

export const CAMERA_DEFAULTS: Settings = {
  volume: 80,
  speech_rate: 1,
  announce_people: true,
  announce_obstacles: true,
  safety_sensitivity: "normal",
  info_min_interval_s: 30,
};

/** Tell the laptop camera how sensitive to be and what to call out (the relay passes it on). */
function sendToCamera(url: string, s: Settings): void {
  try {
    const ws = new WebSocket(cameraWsUrl(url));
    ws.onopen = () => {
      ws.send(JSON.stringify({ type: "proto_settings", sensitivity: s.safety_sensitivity, hazards: s.announce_obstacles, people: s.announce_people }));
      setTimeout(() => ws.close(), 300);
    };
    ws.onerror = () => ws.close();
  } catch {
    /* the camera page will use its defaults */
  }
}

/** A HatApi for the laptop camera: settings on the phone, no history or health (not a hat). */
export function makeCameraApi(url: string): HatApi {
  const notAHat = (what: string) => Promise.reject(new ApiError(0, `${what} come from a paired hat. You're using the laptop camera.`));
  return {
    pair: () => Promise.resolve({ ok: true } as never),
    health: () => notAHat("Hat health details") as Promise<HealthResponse>,
    activity: () => Promise.resolve([]),
    alerts: () => Promise.resolve([]),
    settings: async () => {
      const raw = await AsyncStorage.getItem(SETTINGS_KEY);
      const s = raw ? { ...CAMERA_DEFAULTS, ...(JSON.parse(raw) as Partial<Settings>) } : CAMERA_DEFAULTS;
      sendToCamera(url, s);
      return s;
    },
    saveSettings: async (s: Settings) => {
      await AsyncStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
      sendToCamera(url, s);
      return s;
    },
  };
}
