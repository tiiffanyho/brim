// Shared screen scaffold: an <h1> that receives focus when the screen opens, so VoiceOver and
// TalkBack announce where the user landed (SPA route changes are otherwise silent).
import { useEffect, useRef, type ReactNode } from "react";

export function Screen({ title, children, intro }: { title: string; children: ReactNode; intro?: ReactNode }) {
  const h1 = useRef<HTMLHeadingElement>(null);
  useEffect(() => {
    document.title = `${title} – Brim`;
    h1.current?.focus();
  }, [title]);
  return (
    <section aria-labelledby="screen-title" className="screen">
      <h1 id="screen-title" ref={h1} tabIndex={-1}>
        {title}
      </h1>
      {intro && <p className="intro">{intro}</p>}
      {children}
    </section>
  );
}

export function ErrorMessage({ error }: { error: string | null }) {
  if (!error) return null;
  return (
    <p className="error" role="alert">
      {error}
    </p>
  );
}

export function Loading({ what }: { what: string }) {
  return (
    <p role="status" className="muted">
      Loading {what}…
    </p>
  );
}
