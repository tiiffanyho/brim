// REST client for the hat's app_server. Same endpoints as the web app (app/src/api/client.ts),
// but with an explicit hat address because a native app has no "same origin".
// The session is the HttpOnly cookie set by POST /api/pair; iOS stores it and sends it with
// every request and with the WebSocket upgrade, so JS never sees the token.
import type { ApiError as ApiErrorBody, HealthResponse, PairResponse, Settings } from "@schema";

export const DEFAULT_PORT = 8080;

export class ApiError extends Error {
  constructor(
    public readonly status: number,
    message: string,
    public readonly retryAfterS?: number,
  ) {
    super(message);
    this.name = "ApiError";
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

export function makeApi(hatUrl: string) {
  async function request<T>(method: "GET" | "POST", path: string, body?: unknown): Promise<T> {
    let res: Response;
    try {
      res = await fetch(`${hatUrl}/api${path}`, {
        method,
        headers: body === undefined ? undefined : { "Content-Type": "application/json" },
        body: body === undefined ? undefined : JSON.stringify(body),
        credentials: "include",
      });
    } catch {
      throw new ApiError(0, "Can't reach your hat. Check that your phone is on the same Wi-Fi as the hat.");
    }
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
    settings: () => request<Settings>("GET", "/settings"),
  };
}
export type HatApi = ReturnType<typeof makeApi>;
