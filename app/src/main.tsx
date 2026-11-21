import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { AnnouncerProvider } from "./a11y/Announcer";
import { App } from "./App";
import { BrimProvider } from "./state";
import "@fontsource-variable/figtree";
import "./styles.css";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <AnnouncerProvider>
      <BrimProvider>
        <App />
      </BrimProvider>
    </AnnouncerProvider>
  </StrictMode>,
);

// Offline app shell (production build only; in dev the SW would fight Vite's HMR).
if (import.meta.env.PROD && "serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("/sw.js").catch(() => {
      /* non-fatal: app works without offline shell */
    });
  });
}
