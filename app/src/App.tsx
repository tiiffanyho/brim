import { useEffect, useState } from "react";
import { useBrim } from "./state";
import { ActivityScreen } from "./screens/Activity";
import { AlertsScreen } from "./screens/Alerts";
import { DeviceHealthScreen } from "./screens/DeviceHealth";
import { PairScreen } from "./screens/Pair";
import { SettingsScreen } from "./screens/Settings";

const ROUTES = [
  { path: "activity", label: "Activity", Component: ActivityScreen },
  { path: "alerts", label: "Alerts", Component: AlertsScreen },
  { path: "settings", label: "Settings", Component: SettingsScreen },
  { path: "health", label: "Device health", Component: DeviceHealthScreen },
] as const;
type RoutePath = (typeof ROUTES)[number]["path"];

function currentRoute(): RoutePath {
  const h = window.location.hash.replace(/^#\/?/, "");
  return (ROUTES.find((r) => r.path === h)?.path ?? "activity") as RoutePath;
}

export function App() {
  const { connection, stream, linkLost, unreadAlerts } = useBrim();
  const [route, setRoute] = useState<RoutePath>(currentRoute);

  useEffect(() => {
    const onHash = () => setRoute(currentRoute());
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);

  const banner = linkLost
    ? "Connection lost. Retrying…"
    : connection === "offline"
      ? "Can't reach your hat. Check the phone is on the hat's Wi-Fi. Retrying…"
      : connection === "online" && stream !== "open"
        ? "Reconnecting to live updates…"
        : "";

  const Active = ROUTES.find((r) => r.path === route)!.Component;

  return (
    <div className="app">
      <a href="#main" className="skip-link" onClick={(e) => { e.preventDefault(); document.getElementById("main")?.focus(); }}>
        Skip to content
      </a>
      <header className="topbar">
        <span className="brand" aria-hidden="true">
          Brim
        </span>
        {/* Visible only: the phone says "Connection lost" / "Connected again" itself (state.tsx), so
            this is not a live region (it would read a second, longer sentence on top). */}
        <p className={banner ? "banner" : "visually-hidden"}>
          {banner}
        </p>
      </header>

      <main id="main" tabIndex={-1}>
        {connection === "checking" ? (
          <p role="status">Connecting to your hat…</p>
        ) : connection === "unpaired" ? (
          <PairScreen />
        ) : (
          <Active />
        )}
      </main>

      {connection !== "unpaired" && connection !== "checking" && (
        <nav aria-label="Main" className="tabs">
          <ul>
            {ROUTES.map((r) => {
              const badge = r.path === "alerts" && unreadAlerts > 0 ? unreadAlerts : 0;
              return (
                <li key={r.path}>
                  <a
                    href={`#/${r.path}`}
                    aria-current={route === r.path ? "page" : undefined}
                    aria-label={badge ? `${r.label}, ${badge} new` : undefined}
                  >
                    {r.label}
                    {badge > 0 && (
                      <span className="count" aria-hidden="true">
                        {badge}
                      </span>
                    )}
                  </a>
                </li>
              );
            })}
          </ul>
        </nav>
      )}
    </div>
  );
}
