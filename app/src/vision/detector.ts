// Webcam detector for the laptop camera test: MediaPipe Tasks (web) running EfficientDet-Lite0,
// the same model family as the hat (models/README.md). Dev only: vite.config.ts serves the WASM
// from node_modules and the model from models/ (or, if you haven't copied it there yet, the
// browser downloads Google's copy once). Frames never leave the browser and are never saved.
import { FilesetResolver, ObjectDetector } from "@mediapipe/tasks-vision";
import type { Detection, Thing } from "./nav";

export const LOCAL_MODEL = "/vision-model/efficientdet_lite0.tflite";
// Same bytes as models/README.md's file (mediapipe-tasks/object_detector/efficientdet_lite0_uint8).
export const REMOTE_MODEL =
  "https://storage.googleapis.com/mediapipe-tasks/object_detector/efficientdet_lite0_uint8.tflite";

/** COCO name → Brim Thing, same as device/src/detect/detector.cpp. Others are shown but ignored. */
const THING: Record<string, Thing> = {
  person: "person",
  bicycle: "bicycle",
  car: "vehicle",
  motorcycle: "vehicle",
  bus: "vehicle",
  truck: "vehicle",
  chair: "chair",
  "dining table": "table",
  "cell phone": "phone",
  bench: "unknown",
  backpack: "unknown",
  suitcase: "unknown",
  couch: "unknown",
};
const SHORT: Record<string, string> = { "dining table": "table", "cell phone": "phone", motorcycle: "vehicle", car: "vehicle", bus: "vehicle", truck: "vehicle" };

export interface Seen extends Detection {
  label: string;
  /** false for COCO classes the hat ignores (cup, laptop…): drawn grey, never used for navigation. */
  brim: boolean;
}

export async function loadDetector(minScore: number): Promise<{ detector: ObjectDetector; modelSource: "local" | "google" }> {
  const vision = await FilesetResolver.forVisionTasks("/vision-wasm");
  const local = await fetch(LOCAL_MODEL, { method: "HEAD" }).then((r) => r.ok).catch(() => false);
  const make = (delegate: "GPU" | "CPU") =>
    ObjectDetector.createFromOptions(vision, {
      baseOptions: { modelAssetPath: local ? LOCAL_MODEL : REMOTE_MODEL, delegate },
      runningMode: "VIDEO",
      scoreThreshold: Math.min(minScore, 0.4), // show a little more than the hat acts on
      maxResults: 15,
    });
  const detector = await make("GPU").catch(() => make("CPU"));
  return { detector, modelSource: local ? "local" : "google" };
}

/** Run one frame. Boxes come back as fractions of the frame, like the hat's Box. */
export function detect(detector: ObjectDetector, video: HTMLVideoElement, nowMs: number): Seen[] {
  const w = video.videoWidth;
  const h = video.videoHeight;
  if (!w || !h) return [];
  const res = detector.detectForVideo(video, nowMs);
  const out: Seen[] = [];
  for (const d of res.detections) {
    const c = d.categories[0];
    const b = d.boundingBox;
    if (!c || !b) continue;
    const name = c.categoryName || "object";
    const thing = THING[name];
    out.push({
      thing: thing ?? "unknown",
      brim: thing !== undefined,
      label: SHORT[name] ?? name,
      score: c.score,
      box: {
        x: Math.max(0, b.originX / w),
        y: Math.max(0, b.originY / h),
        w: Math.min(1, b.width / w),
        h: Math.min(1, b.height / h),
      },
    });
  }
  return out;
}
