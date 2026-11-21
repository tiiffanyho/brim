import path from "node:path";
import { defineConfig } from "vitest/config";

// Pure-logic tests only (src/*.test.ts). Same aliases as metro.config.js.
export default defineConfig({
  resolve: {
    alias: {
      "@schema": path.resolve(__dirname, "../shared/gen/events.ts"),
    },
  },
  test: { include: ["src/**/*.test.ts"] },
});
