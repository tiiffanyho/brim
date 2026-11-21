/// <reference types="vitest/config" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";

// In dev, Vite serves the app and proxies /api + /ws to app_server so the session cookie is
// same-origin, exactly like production (where app_server serves app/dist itself).
const appServer = process.env.BRIM_APP_SERVER ?? "http://127.0.0.1:8080";

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: { "@schema": fileURLToPath(new URL("../shared/gen/events.ts", import.meta.url)) },
  },
  server: {
    port: 5173,
    fs: { allow: [".."] },
    proxy: {
      "/api": { target: appServer, changeOrigin: false },
      "/ws": { target: appServer.replace(/^http/, "ws"), ws: true },
    },
  },
  build: {
    outDir: "dist",
    sourcemap: true,
    target: "es2020",
    // Bake the Figtree font into the CSS: app_server's static mount may not know the .woff2 type.
    assetsInlineLimit: (file) => (file.endsWith(".woff2") ? true : undefined),
  },
  test: { environment: "node", include: ["src/**/*.test.ts"] },
});
