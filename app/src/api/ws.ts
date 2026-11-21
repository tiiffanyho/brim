// WebSocket client for live events, with automatic reconnect (exponential backoff + jitter).
// Only paired phones can connect (the server checks the session cookie on upgrade); a failed
// upgrade looks like a close, so the app re-checks /api/health to tell "unpaired" from "offline".
import { isBrimEvent, type BrimEvent } from "@schema";

export type StreamStatus = "connecting" | "open" | "closed";

export interface StreamOptions {
  url: string;
  onEvent: (e: BrimEvent) => void;
  onStatus?: (s: StreamStatus) => void;
  /** Injected for tests. */
  WebSocketImpl?: typeof WebSocket;
  minDelayMs?: number;
  maxDelayMs?: number;
  random?: () => number;
}

export const WS_PROTOCOL = "brim-events";

export function backoffDelay(attempt: number, min: number, max: number, random: () => number): number {
  const exp = Math.min(max, min * 2 ** attempt);
  return Math.round(exp / 2 + (random() * exp) / 2); // "equal jitter"
}

export class EventStream {
  private ws: WebSocket | null = null;
  private attempt = 0;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;
  private readonly opts: Required<Omit<StreamOptions, "onStatus">> & Pick<StreamOptions, "onStatus">;

  constructor(opts: StreamOptions) {
    this.opts = {
      WebSocketImpl: globalThis.WebSocket,
      minDelayMs: 500,
      maxDelayMs: 10_000,
      random: Math.random,
      ...opts,
    };
  }

  start(): void {
    this.stopped = false;
    this.connect();
  }

  stop(): void {
    this.stopped = true;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.ws?.close();
    this.ws = null;
  }

  /** Reconnect now (e.g. after pairing or when the page becomes visible). */
  reconnectNow(): void {
    if (this.stopped) return;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.attempt = 0;
    if (this.ws && this.ws.readyState <= 1) return;
    this.connect();
  }

  private connect(): void {
    this.opts.onStatus?.("connecting");
    let ws: WebSocket;
    try {
      ws = new this.opts.WebSocketImpl(this.opts.url, WS_PROTOCOL);
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.ws = ws;
    ws.onopen = () => {
      this.attempt = 0;
      this.opts.onStatus?.("open");
    };
    ws.onmessage = (msg: MessageEvent) => {
      try {
        const data: unknown = JSON.parse(String(msg.data));
        if (isBrimEvent(data)) this.opts.onEvent(data);
      } catch {
        /* ignore malformed frames */
      }
    };
    ws.onclose = () => {
      if (this.ws === ws) this.ws = null;
      this.opts.onStatus?.("closed");
      this.scheduleReconnect();
    };
    ws.onerror = () => ws.close();
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.timer) return;
    const delay = backoffDelay(this.attempt++, this.opts.minDelayMs, this.opts.maxDelayMs, this.opts.random);
    this.timer = setTimeout(() => {
      this.timer = null;
      if (!this.stopped) this.connect();
    }, delay);
  }
}

export function defaultWsUrl(loc: Location = window.location): string {
  return `${loc.protocol === "https:" ? "wss" : "ws"}://${loc.host}/ws`;
}
