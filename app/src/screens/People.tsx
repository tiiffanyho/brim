import { useCallback, useEffect, useRef, useState, type FormEvent } from "react";
import type { Person } from "@schema";
import { api } from "../api/client";
import { useAnnouncer } from "../a11y/Announcer";
import { relativeTime } from "../a11y/describe";
import { useLiveEvents } from "../state";
import { ErrorMessage, Loading, Screen } from "./Screen";

export function PeopleScreen() {
  const [people, setPeople] = useState<Person[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const { announce } = useAnnouncer();
  const listHeading = useRef<HTMLHeadingElement>(null);

  const load = useCallback(async () => {
    try {
      setPeople(await api.people());
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Couldn't load people.");
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);

  useLiveEvents((e) => {
    if (e.type !== "person_recognized") return;
    setPeople((ps) => ps?.map((p) => (p.id === e.person_id ? { ...p, last_seen_at: new Date().toISOString() } : p)) ?? ps);
  });

  async function rename(p: Person, name: string): Promise<boolean> {
    try {
      const updated = await api.renamePerson(p.id, name);
      setPeople((ps) => ps?.map((x) => (x.id === p.id ? updated : x)) ?? ps);
      announce(`Renamed to ${updated.display_name}.`);
      return true;
    } catch (e) {
      setError(e instanceof Error ? e.message : "Rename failed.");
      return false;
    }
  }

  async function forget(p: Person) {
    try {
      await api.forgetPerson(p.id);
      setPeople((ps) => ps?.filter((x) => x.id !== p.id) ?? ps);
      announce(`${p.display_name} forgotten. Their face fingerprint and name recording were deleted from the hat.`);
      listHeading.current?.focus();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Forget failed.");
    }
  }

  return (
    <Screen title="People" intro="People the hat knows by name. Everyone else stays anonymous and is forgotten after 30 minutes.">
      <ErrorMessage error={error} />
      {people === null && !error && <Loading what="people" />}
      {people && (
        <>
          <h2 ref={listHeading} tabIndex={-1} className="visually-hidden">
            {people.length === 1 ? "1 person" : `${people.length} people`}
          </h2>
          {people.length === 0 ? (
            <p>No one is enrolled yet. Enrollment happens on the hat.</p>
          ) : (
            <ul className="cards">
              {people.map((p) => (
                <PersonCard key={p.id} person={p} onRename={rename} onForget={forget} />
              ))}
            </ul>
          )}
        </>
      )}
      {/* TODO: enrollment flow ("Teach the hat a new person") once camera/AI lands on the device. */}
    </Screen>
  );
}

function PersonCard({
  person: p,
  onRename,
  onForget,
}: {
  person: Person;
  onRename: (p: Person, name: string) => Promise<boolean>;
  onForget: (p: Person) => Promise<void>;
}) {
  const [mode, setMode] = useState<"view" | "rename" | "confirm">("view");
  const [name, setName] = useState(p.display_name);
  const inputRef = useRef<HTMLInputElement>(null);
  const confirmRef = useRef<HTMLButtonElement>(null);
  const renameBtn = useRef<HTMLButtonElement>(null);
  const forgetBtn = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (mode === "rename") inputRef.current?.focus();
    if (mode === "confirm") confirmRef.current?.focus();
  }, [mode]);

  async function submit(e: FormEvent) {
    e.preventDefault();
    if (!name.trim()) return;
    if (await onRename(p, name.trim())) {
      setMode("view");
      setTimeout(() => renameBtn.current?.focus(), 0);
    }
  }

  const lastSeen = p.last_seen_at ? `Last seen ${relativeTime(p.last_seen_at)}` : "Not seen yet";
  return (
    <li className="card">
      <h2>{p.display_name}</h2>
      <p className="muted">
        {lastSeen}. {p.has_name_clip ? "Name recording saved." : "No name recording."}
      </p>
      {mode === "view" && (
        <div className="row">
          <button ref={renameBtn} type="button" onClick={() => setMode("rename")} aria-label={`Rename ${p.display_name}`}>
            Rename
          </button>
          <button ref={forgetBtn} type="button" className="danger" onClick={() => setMode("confirm")} aria-label={`Forget ${p.display_name}`}>
            Forget
          </button>
        </div>
      )}
      {mode === "rename" && (
        <form onSubmit={submit}>
          <label htmlFor={`name-${p.id}`}>New name for {p.display_name}</label>
          <input id={`name-${p.id}`} ref={inputRef} value={name} maxLength={60} onChange={(e) => setName(e.target.value)} />
          <div className="row">
            <button type="submit" className="primary">
              Save
            </button>
            <button
              type="button"
              onClick={() => {
                setName(p.display_name);
                setMode("view");
                setTimeout(() => renameBtn.current?.focus(), 0);
              }}
            >
              Cancel
            </button>
          </div>
        </form>
      )}
      {mode === "confirm" && (
        <div role="group" aria-labelledby={`confirm-${p.id}`} className="confirm">
          <p id={`confirm-${p.id}`}>
            Forget {p.display_name}? This deletes their face fingerprint and name recording from the hat. It can't be undone.
          </p>
          <div className="row">
            <button ref={confirmRef} type="button" className="danger" onClick={() => void onForget(p)}>
              Yes, forget {p.display_name}
            </button>
            <button
              type="button"
              onClick={() => {
                setMode("view");
                setTimeout(() => forgetBtn.current?.focus(), 0);
              }}
            >
              Cancel
            </button>
          </div>
        </div>
      )}
    </li>
  );
}
