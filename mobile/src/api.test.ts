// Run with `npm test` (vitest; pure logic, no React Native needed).
import { expect, test } from "vitest";
import { normalizeHatUrl, wsUrlFor } from "./api";

test("bare IP gets http and the default port", () => {
  expect(normalizeHatUrl("192.168.1.42")).toBe("http://192.168.1.42:8080");
});

test("keeps an explicit port and scheme, trims spaces and a trailing slash", () => {
  expect(normalizeHatUrl("  192.168.1.42:9000 ")).toBe("http://192.168.1.42:9000");
  expect(normalizeHatUrl("http://Brim.local:8080/")).toBe("http://brim.local:8080");
  expect(normalizeHatUrl("https://hat.test")).toBe("https://hat.test:8080");
});

test("rejects anything that is not host[:port]", () => {
  for (const bad of ["", "   ", "http://", "192.168.1.42/api", "a b", "192.168.1.42:0", "192.168.1.42:70000", "ftp://x"])
    expect(normalizeHatUrl(bad), bad).toBeNull();
});

test("WebSocket URL follows the hat URL", () => {
  expect(wsUrlFor("http://192.168.1.42:8080")).toBe("ws://192.168.1.42:8080/ws");
  expect(wsUrlFor("https://hat.test:8080")).toBe("wss://hat.test:8080/ws");
});
