// Typed REST client for app_server. All types come from shared/events.schema.json via @schema.
// The session is an HttpOnly cookie set by POST /api/pair; JS never sees the token.
import type {
  ActivityItem,
  Alert,
  ApiError as ApiErrorBody,
  HealthResponse,
  PairResponse,
  Person,
  PersonPatch,
  Settings,
} from "@schema";

// TODO(qnx-check): if the QNX libwebsockets build lacks PUT/PATCH/DELETE (see
// device/src/app_server/app_server_main.cpp), set this to true: those calls are then sent as
// POST + X-HTTP-Method-Override.
export const USE_METHOD_OVERRIDE = false;

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

type Method = "GET" | "POST" | "PUT" | "PATCH" | "DELETE";

async function request<T>(method: Method, path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = {};
  let sendMethod: Method = method;
  if (USE_METHOD_OVERRIDE && (method === "PUT" || method === "PATCH" || method === "DELETE")) {
    headers["X-HTTP-Method-Override"] = method;
    sendMethod = "POST";
  }
  if (body !== undefined) headers["Content-Type"] = "application/json";
  let res: Response;
  try {
    res = await fetch(`/api${path}`, {
      method: sendMethod,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      credentials: "same-origin",
      cache: "no-store",
    });
  } catch {
    throw new ApiError(0, "Can't reach your hat. Check that your phone is on the hat's Wi-Fi.");
  }
  if (res.status === 204) return undefined as T;
  const text = await res.text();
  const json: unknown = text ? JSON.parse(text) : undefined;
  if (!res.ok) {
    const err = (json ?? {}) as Partial<ApiErrorBody>;
    throw new ApiError(res.status, err.error ?? `Request failed (${res.status})`, err.retry_after_s);
  }
  return json as T;
}

export const api = {
  pair: (pin: string) => request<PairResponse>("POST", "/pair", { pin }),
  health: () => request<HealthResponse>("GET", "/health"),
  people: () => request<Person[]>("GET", "/people"),
  renamePerson: (id: number, display_name: string) =>
    request<Person>("PATCH", `/people/${id}`, { display_name } satisfies PersonPatch),
  forgetPerson: (id: number) => request<void>("DELETE", `/people/${id}`),
  activity: (limit = 50) => request<ActivityItem[]>("GET", `/activity?limit=${limit}`),
  alerts: (limit = 50) => request<Alert[]>("GET", `/alerts?limit=${limit}`),
  settings: () => request<Settings>("GET", "/settings"),
  saveSettings: (s: Settings) => request<Settings>("PUT", "/settings", s),
};
