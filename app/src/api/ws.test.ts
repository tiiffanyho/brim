import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { backoffDelay, EventStream, WS_PROTOCOL } from "./ws";

class FakeWS {
  static instances: FakeWS[] = [];
  readyState = 0;
  onopen: (() => void) | null = null;
  onmessage: ((m: { data: string }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  constructor(
    public url: string,
    public protocol: string,
  ) {
    FakeWS.instances.push(this);
  }
  open() {
    this.readyState = 1;
    this.onopen?.();
  }
  close() {
    this.readyState = 3;
    this.onclose?.();
  }
  send() {}
}

describe("backoffDelay", () => {
  it("grows exponentially and caps", () => {
    const r = () => 1;
    expect(backoffDelay(0, 500, 10_000, r)).toBe(500);
    expect(backoffDelay(3, 500, 10_000, r)).toBe(4000);
    expect(backoffDelay(10, 500, 10_000, r)).toBe(10_000);
    expect(backoffDelay(10, 500, 10_000, () => 0)).toBe(5000); // jitter floor = half
  });
});

describe("EventStream", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    FakeWS.instances = [];
  });
  afterEach(() => vi.useRealTimers());

  function make() {
    const events: unknown[] = [];
    const statuses: string[] = [];
    const s = new EventStream({
      url: "ws://hat/ws",
      onEvent: (e) => events.push(e),
      onStatus: (st) => statuses.push(st),
      WebSocketImpl: FakeWS as unknown as typeof WebSocket,
      random: () => 1,
    });
    return { s, events, statuses };
  }

  it("connects with the brim-events subprotocol and delivers valid events only", () => {
    const { s, events, statuses } = make();
    s.start();
    const ws = FakeWS.instances[0]!;
    expect(ws.protocol).toBe(WS_PROTOCOL);
    ws.open();
    ws.onmessage?.({
      data: JSON.stringify({ type: "clear_path", seq: 1, source: "brim", t_mono_ms: 1, clear: true, direction: "ahead", text: "Clear path ahead" }),
    });
    ws.onmessage?.({ data: JSON.stringify({ type: "nope" }) });
    ws.onmessage?.({ data: JSON.stringify({ type: "person_recognized" }) }); // removed event type
    ws.onmessage?.({ data: "not json" });
    expect(events).toHaveLength(1);
    expect(statuses).toEqual(["connecting", "open"]);
  });

  it("reconnects with backoff after close, resets after success", () => {
    const { s } = make();
    s.start();
    FakeWS.instances[0]!.close();
    vi.advanceTimersByTime(499);
    expect(FakeWS.instances).toHaveLength(1);
    vi.advanceTimersByTime(1);
    expect(FakeWS.instances).toHaveLength(2);
    FakeWS.instances[1]!.close();
    vi.advanceTimersByTime(999);
    expect(FakeWS.instances).toHaveLength(2);
    vi.advanceTimersByTime(1);
    expect(FakeWS.instances).toHaveLength(3);
    FakeWS.instances[2]!.open(); // success resets backoff
    FakeWS.instances[2]!.close();
    vi.advanceTimersByTime(500);
    expect(FakeWS.instances).toHaveLength(4);
  });

  it("stop() prevents reconnects", () => {
    const { s } = make();
    s.start();
    s.stop();
    vi.advanceTimersByTime(60_000);
    expect(FakeWS.instances).toHaveLength(1);
  });

  it("forceReconnect() drops a socket that looks open but is dead, and connects again now", () => {
    const { s, events, statuses } = make();
    s.start();
    const dead = FakeWS.instances[0]!;
    dead.open();
    s.forceReconnect();
    expect(FakeWS.instances).toHaveLength(2);
    expect(statuses.slice(-2)).toEqual(["closed", "connecting"]);
    // The old socket's late messages and close are ignored.
    dead.onmessage?.({ data: JSON.stringify({ type: "health" }) });
    expect(dead.onclose).toBeNull();
    FakeWS.instances[1]!.open();
    expect(statuses.at(-1)).toBe("open");
    expect(events).toHaveLength(0);
    s.stop();
  });
});
