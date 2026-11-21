// The phone's own voice. Unlike the web app (which hands text to VoiceOver through ARIA live
// regions, and goes silent when the page is suspended), the native app speaks with the system
// speech engine, so it keeps talking with the screen locked or another app in front.
//
// Background on iOS: the app declares the `audio` background mode (expo-audio plugin in app.json)
// and, while "on duty", loops a silent clip with the audio session set to play in the background.
// That keeps the app (and its WebSocket and timers) running when the phone is locked; the speech
// engine then talks over it using the same app audio session.
// TODO(hw): confirm on a real iPhone: lock the phone for 10+ minutes on battery and check the
//   warnings keep coming. The simulator does not suspend apps like a real device does.
// TODO(android): needs the media foreground service + setActiveForLockScreen (expo-audio docs:
//   otherwise Android stops background audio after ~3 minutes). Not done yet: iPhone first.
import { createAudioPlayer, setAudioModeAsync, type AudioPlayer } from "expo-audio";
import * as Speech from "expo-speech";
import type { Politeness } from "@web/a11y/describe";

const silence = require("../assets/silence.wav") as number;

let player: AudioPlayer | null = null;
let rate = 1.0;

/** Speech rate from the hat's settings (0.5 - 2.0, 1.0 normal). */
export function setSpeechRate(r: number): void {
  rate = Math.min(2, Math.max(0.5, r));
}

/**
 * Says a phrase. "assertive" (safety alerts) cuts off whatever is being said; "polite" queues
 * behind it. Ordering is decided by SpeechScheduler before this is called.
 */
export function say(text: string, politeness: Politeness): void {
  if (politeness === "assertive") void Speech.stop();
  Speech.speak(text, {
    rate,
    // Speak on the app's own audio session, which is active in the background and in silent mode.
    useApplicationAudioSession: true,
  });
}

/** Keep the app alive in the background so warnings still arrive and are spoken. */
export async function startOnDuty(): Promise<void> {
  if (player) return;
  await setAudioModeAsync({
    playsInSilentMode: true,
    shouldPlayInBackground: true,
    // Mix with the wearer's music or VoiceOver instead of stopping them.
    interruptionMode: "mixWithOthers",
  });
  const p = createAudioPlayer(silence);
  p.loop = true;
  p.play();
  player = p;
}

export function stopOnDuty(): void {
  if (!player) return;
  player.pause();
  player.remove();
  player = null;
  void Speech.stop();
}

export function onDuty(): boolean {
  return player !== null;
}
