// Laptop camera ⇄ phone link over the dev server's /camlink relay (vite.config.ts). The camera
// sends the hat's own event shapes, so the phone shows them with the normal Live screen.
import type { BrimEvent } from "@schema";

/** Phone -> camera: the wearer's choices that change what the camera calls out. */
export interface ProtoSettings {
  type: "proto_settings";
  sensitivity: "low" | "normal" | "high";
  hazards: boolean;
  people: boolean;
}

export type LinkMsg = BrimEvent | ProtoSettings | { type: "peers"; phones: number };

export interface Link {
  send(e: BrimEvent | ProtoSettings): void;
  close(): void;
}

export function openLink(
  role: "camera" | "phone",
  onMessage: (m: LinkMsg) => void,
  onOpen: (open: boolean) => void,
): Link {
  let ws: WebSocket | null = null;
  let closed = false;
  let retry: ReturnType<typeof setTimeout> | undefined;
  const url = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/camlink?role=${role}`;

  const connect = () => {
    if (closed) return;
    ws = new WebSocket(url);
    ws.onopen = () => onOpen(true);
    ws.onmessage = (m) => {
      try {
        onMessage(JSON.parse(String(m.data)) as LinkMsg);
      } catch {
        /* ignore anything that isn't JSON */
      }
    };
    ws.onclose = () => {
      onOpen(false);
      if (!closed) retry = setTimeout(connect, 500); // reconnect fast: this is a live demo
    };
    ws.onerror = () => ws?.close();
  };
  connect();

  return {
    send(e) {
      if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(e));
    },
    close() {
      closed = true;
      clearTimeout(retry);
      ws?.close();
    },
  };
}
