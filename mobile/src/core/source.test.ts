import { describe, expect, it } from "vitest";
import { cameraWsUrl, levelAllows, normalizeCameraUrl } from "./source";

describe("laptop camera address", () => {
  it("accepts what the camera page shows", () => {
    expect(normalizeCameraUrl("192.168.1.20")).toBe("http://192.168.1.20:5173");
    expect(normalizeCameraUrl(" 192.168.1.20:5199 ")).toBe("http://192.168.1.20:5199");
    expect(normalizeCameraUrl("http://192.168.1.20:5173/#/phone")).toBe("http://192.168.1.20:5173");
  });
  it("refuses things that aren't an address", () => {
    expect(normalizeCameraUrl("")).toBeNull();
    expect(normalizeCameraUrl("my laptop")).toBeNull();
    expect(normalizeCameraUrl("1.2.3.4:99999")).toBeNull();
  });
  it("points the live stream at the relay", () => {
    expect(cameraWsUrl("http://192.168.1.20:5173")).toBe("ws://192.168.1.20:5173/camlink?role=phone");
  });
});

describe("how much Brim does", () => {
  it("detect only says nothing", () => {
    for (const t of ["alert", "nav", "status", "info"] as const) expect(levelAllows(0, t)).toBe(false);
  });
  it("warn says safety and status, never directions or info", () => {
    expect(levelAllows(1, "alert")).toBe(true);
    expect(levelAllows(1, "status")).toBe(true);
    expect(levelAllows(1, "nav")).toBe(false);
    expect(levelAllows(1, "info")).toBe(false);
  });
  it("warn and guide says everything", () => {
    for (const t of ["alert", "nav", "status", "info"] as const) expect(levelAllows(2, t)).toBe(true);
  });
});
