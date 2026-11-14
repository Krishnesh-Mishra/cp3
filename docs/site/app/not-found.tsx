import Link from 'next/link';

export default function NotFound() {
  return (
    <main className="flex flex-1 flex-col items-center justify-center gap-6 px-6 py-24 text-center">
      <p className="text-sm font-semibold uppercase tracking-[0.2em] text-fd-muted-foreground">
        404
      </p>
      <h1 className="text-4xl font-bold tracking-tight text-fd-foreground">This page does not exist</h1>
      <p className="max-w-md text-fd-muted-foreground">
        The page you are looking for was moved or never existed. Try the documentation overview instead.
      </p>
      <div className="flex flex-wrap items-center justify-center gap-3">
        <Link
          href="/"
          className="rounded-lg bg-fd-primary px-5 py-2.5 text-sm font-medium text-fd-primary-foreground transition-opacity hover:opacity-90"
        >
          Home
        </Link>
        <Link
          href="/overview"
          className="rounded-lg border border-fd-border bg-fd-card px-5 py-2.5 text-sm font-medium text-fd-foreground transition-colors hover:bg-fd-accent hover:text-fd-accent-foreground"
        >
          Documentation overview
        </Link>
      </div>
    </main>
  );
}
