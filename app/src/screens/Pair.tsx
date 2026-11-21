import { useState, type FormEvent } from "react";
import { api, ApiError } from "../api/client";
import { useBrim } from "../state";
import { ErrorMessage, Screen } from "./Screen";

export function PairScreen() {
  const { onPaired } = useBrim();
  const [pin, setPin] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    if (!/^\d{6}$/.test(pin)) {
      setError("The PIN is 6 digits.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await api.pair(pin);
      onPaired();
    } catch (err) {
      if (err instanceof ApiError && err.status === 429)
        setError(`Too many wrong tries. Wait ${err.retryAfterS ?? 60} seconds and try again.`);
      else if (err instanceof ApiError && err.status === 401) setError("That PIN didn't match. Check the hat and try again.");
      else setError(err instanceof Error ? err.message : "Pairing failed.");
      setPin("");
    } finally {
      setBusy(false);
    }
  }

  return (
    <Screen title="Pair with your hat" intro="Ask someone to read you the 6-digit PIN shown on the hat's display, or listen for it.">
      <form onSubmit={submit} noValidate>
        <label htmlFor="pin">6-digit PIN</label>
        <input
          id="pin"
          name="pin"
          inputMode="numeric"
          autoComplete="one-time-code"
          pattern="[0-9]{6}"
          maxLength={6}
          value={pin}
          onChange={(e) => setPin(e.target.value.replace(/\D/g, ""))}
          aria-describedby="pin-help"
          aria-invalid={error ? true : undefined}
        />
        <p id="pin-help" className="muted">
          Your phone must be on the same Wi-Fi as the hat. Nothing is sent to the internet.
        </p>
        <ErrorMessage error={error} />
        <button type="submit" className="primary" disabled={busy}>
          {busy ? "Pairing…" : "Pair"}
        </button>
      </form>
    </Screen>
  );
}
