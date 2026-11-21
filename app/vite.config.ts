/// <reference types="vitest/config" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";
import { createReadStream, existsSync, statSync } from "node:fs";
import { join, normalize } from "node:path";
import { networkInterfaces } from "node:os";
import type { Plugin } from "vite";
import { WebSocketServer, WebSocket } from "ws";

/** This laptop's Wi-Fi addresses, so the camera page can show the phone a link. */
function lanAddresses(): string[] {
  return Object.values(networkInterfaces())
    .flat()
    .filter((a) => a && a.family === "IPv4" && !a.internal)
    .map((a) => a!.address);
}

// Dev only: links the laptop camera page (#/camera) to phones (#/phone) on the same Wi-Fi.
// Whatever the camera sends goes straight to every phone; the camera hears how many phones are on.
function cameraLink(): Plugin {
  return {
    name: "brim-camera-link",
    apply: "serve",
    configureServer(server) {
      const wss = new WebSocketServer({ noServer: true, perMessageDeflate: false });
      const cameras = new Set<WebSocket>();
      const phones = new Set<WebSocket>();
      const tellCameras = () => {
        const msg = JSON.stringify({ type: "peers", phones: phones.size });
        for (const c of cameras) if (c.readyState === WebSocket.OPEN) c.send(msg);
      };
      server.httpServer?.on("upgrade", (req, socket, head) => {
        const url = new URL(req.url ?? "/", "http://x");
        if (url.pathname !== "/camlink") return; // Vite HMR and the /ws proxy handle the rest
        wss.handleUpgrade(req, socket, head, (ws) => {
          const role = url.searchParams.get("role") === "camera" ? "camera" : "phone";
          const set = role === "camera" ? cameras : phones;
          set.add(ws);
          tellCameras();
          ws.on("message", (data) => {
            if (role !== "camera") return;
            const text = data.toString();
            for (const p of phones) if (p.readyState === WebSocket.OPEN) p.send(text);
          });
          ws.on("close", () => {
            set.delete(ws);
            tellCameras();
          });
        });
      });
      server.middlewares.use("/camlink-info", (_req, res) => {
        const port = server.config.server.port ?? 5173;
        res.setHeader("Content-Type", "application/json");
        res.end(JSON.stringify({ urls: lanAddresses().map((ip) => `http://${ip}:${port}/#/phone`) }));
      });
    },
  };
}

// Dev only: the laptop camera test (#/camera) loads MediaPipe's WASM from node_modules and the
// detector model from models/ on this machine, so nothing is fetched from a CDN. Not in the build.
function cameraTestFiles(): Plugin {
  const roots: Record<string, string> = {
    "/vision-wasm/": fileURLToPath(new URL("./node_modules/@mediapipe/tasks-vision/wasm/", import.meta.url)),
    "/vision-model/": fileURLToPath(new URL("../models/", import.meta.url)),
  };
  const types: Record<string, string> = { ".js": "text/javascript", ".wasm": "application/wasm", ".tflite": "application/octet-stream" };
  return {
    name: "brim-camera-test-files",
    apply: "serve",
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = (req.url ?? "").split("?")[0] ?? "";
        const prefix = Object.keys(roots).find((p) => url.startsWith(p));
        if (!prefix) return next();
        const root = roots[prefix]!;
        const file = normalize(join(root, decodeURIComponent(url.slice(prefix.length))));
        if (!file.startsWith(root) || !existsSync(file) || !statSync(file).isFile()) {
          res.statusCode = 404;
          return res.end();
        }
        const ext = file.slice(file.lastIndexOf("."));
        res.setHeader("Content-Type", types[ext] ?? "application/octet-stream");
        res.setHeader("Content-Length", statSync(file).size);
        if (req.method === "HEAD") return res.end();
        createReadStream(file).pipe(res);
      });
    },
  };
}

// In dev, Vite serves the app and proxies /api + /ws to app_server so the session cookie is
// same-origin, exactly like production (where app_server serves app/dist itself).
const appServer = process.env.BRIM_APP_SERVER ?? "http://127.0.0.1:8080";

export default defineConfig({
  plugins: [react(), cameraTestFiles(), cameraLink()],
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
