// REST client for the hat's app_server. Same endpoints as the web app (app/src/api/client.ts),
// but with an explicit hat address because a native app has no "same origin".
// The session is the HttpOnly cookie set by POST /api/pair; iOS stores it and sends it with
// every request and with the WebSocket upgrade, so JS never sees the token.
import type { ActivityItem, Alert, ApiError as ApiErrorBody, HealthResponse, PairResponse, Settings } from "@schema";

export const DEFAULT_PORT = 8080;

// TODO(qnx-check): same switch as app/src/api/client.ts. If the QNX libwebsockets build lacks PUT,
// set this to true: PUT is then sent as POST + X-HTTP-Method-Override.
export const USE_METHOD_OVERRIDE = false;

export class ApiError extends Error {
  readonly status: number;
  readonly retryAfterS?: number;
  constructor(status: number, message: string, retryAfterS?: number) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.retryAfterS = retryAfterS;
  }
  get unauthorized(): boolean {
    return this.status === 401;
  }
}

/**
 * "192.168.1.42", "192.168.1.42:8080", "http://brim.local:8080/" -> "http://192.168.1.42:8080".
 * Returns null for anything that is not a plain host[:port]. The hat is only ever on the local
 * network, so plain http is expected.
 */
export function normalizeHatUrl(input: string): string | null {
  let s = input.trim();
  if (!s) return null;
  if (!/^https?:\/\//i.test(s)) s = `http://${s}`;
  const m = /^(https?):\/\/([A-Za-z0-9.-]+|\[[0-9A-Fa-f:]+\])(?::(\d{1,5}))?\/?$/.exec(s);
  if (!m) return null;
  const port = m[3] ? Number(m[3]) : DEFAULT_PORT;
  if (port < 1 || port > 65535) return null;
  return `${m[1]!.toLowerCase()}://${m[2]!.toLowerCase()}:${port}`;
}

export function wsUrlFor(hatUrl: string): string {
  return `${hatUrl.replace(/^http/, "ws")}/ws`;
}

type Method = "GET" | "POST" | "PUT";

export function makeApi(hatUrl: string) {
  async function request<T>(method: Method, path: string, body?: unknown): Promise<T> {
    const headers: Record<string, string> = {};
    let sendMethod: Method = method;
    if (USE_METHOD_OVERRIDE && method === "PUT") {
      headers["X-HTTP-Method-Override"] = method;
      sendMethod = "POST";
    }
    if (body !== undefined) headers["Content-Type"] = "application/json";
    let res: Response;
    try {
      res = await fetch(`${hatUrl}/api${path}`, {
        method: sendMethod,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        credentials: "include",
      });
    } catch {
      throw new ApiError(0, "Can't reach your hat. Check that your phone is on the same Wi-Fi as the hat.");
    }
    if (res.status === 204) return undefined as T;
    const text = await res.text();
    let json: unknown;
    try {
      json = text ? JSON.parse(text) : undefined;
    } catch {
      json = undefined;
    }
    if (!res.ok) {
      const err = (json ?? {}) as Partial<ApiErrorBody>;
      throw new ApiError(res.status, err.error ?? `Request failed (${res.status})`, err.retry_after_s);
    }
    return json as T;
  }
  return {
    pair: (pin: string) => request<PairResponse>("POST", "/pair", { pin }),
    health: () => request<HealthResponse>("GET", "/health"),
    activity: (limit = 100) => request<ActivityItem[]>("GET", `/activity?limit=${limit}`),
    alerts: (limit = 100) => request<Alert[]>("GET", `/alerts?limit=${limit}`),
    settings: () => request<Settings>("GET", "/settings"),
    saveSettings: (s: Settings) => request<Settings>("PUT", "/settings", s),
  };
}
export type HatApi = ReturnType<typeof makeApi>;

/** A message for the wearer from any error thrown by the client. */
export function errorText(e: unknown, fallback: string): string {
  return e instanceof Error && e.message ? e.message : fallback;
}
