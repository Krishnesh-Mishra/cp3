import { HomeLayout } from 'fumadocs-ui/layouts/home';
import { baseOptions } from '@/lib/layout.shared';
import Link from 'next/link';
import type { ReactNode } from 'react';

export default function HomePage() {
  return (
    <HomeLayout {...baseOptions()}>
      <main className="flex flex-1 flex-col">
        <section className="flex flex-1 flex-col items-center justify-center gap-8 px-6 py-20 text-center">
          <p className="text-sm font-semibold uppercase tracking-[0.2em] text-fd-muted-foreground">
            CP3 Documentation
          </p>
          <h1 className="max-w-4xl text-5xl font-bold tracking-tight text-fd-foreground sm:text-6xl">
            Familiar syntax.
            <br />
            <span className="text-fd-muted-foreground">Static checks. Native executables.</span>
          </h1>
          <p className="max-w-2xl text-lg leading-8 text-fd-muted-foreground">
            CP3 is a statically typed language with a JavaScript- and TypeScript-shaped surface.
            It compiles <code className="rounded bg-fd-muted px-1.5 py-0.5 text-sm">.cp3</code>{' '}
            programs to C++ and uses a native toolchain to produce the executable you run.
          </p>
          <div className="flex flex-wrap items-center justify-center gap-3">
            <Link
              href="/quick-start"
              className="rounded-lg bg-fd-primary px-5 py-2.5 text-sm font-medium text-fd-primary-foreground transition-opacity hover:opacity-90"
            >
              Get started
            </Link>
            <Link
              href="/for-js-developers"
              className="rounded-lg border border-fd-border bg-fd-card px-5 py-2.5 text-sm font-medium text-fd-foreground transition-colors hover:bg-fd-accent hover:text-fd-accent-foreground"
            >
              For JS & TypeScript developers
            </Link>
            <a
              href="https://github.com/Krishnesh-Mishra/cp3"
              target="_blank"
              rel="noreferrer"
              className="rounded-lg border border-fd-border bg-fd-card px-5 py-2.5 text-sm font-medium text-fd-foreground transition-colors hover:bg-fd-accent hover:text-fd-accent-foreground"
            >
              GitHub
            </a>
          </div>
        </section>

        <section className="border-t border-fd-border px-6 py-14">
          <div className="mx-auto grid w-full max-w-5xl gap-6 sm:grid-cols-3">
            <Feature
              title="Static, early checks"
              body="Type and shape errors are rejected at compile time, before your program ever runs. Diagnostics name the problem and point at the fix."
            />
            <Feature
              title="Native executables"
              body="Your .cp3 program becomes C++, compiled by a native toolchain. No interpreter and no runtime required to ship the binary."
            />
            <Feature
              title="JavaScript & TypeScript shape"
              body="Familiar syntax, classes, modules, and generics. If you know TS, you can read CP3 on day one."
            />
          </div>
        </section>

        <section className="border-t border-fd-border px-6 py-10">
          <nav
            aria-label="Documentation quick links"
            className="mx-auto flex w-full max-w-5xl flex-wrap items-center justify-center gap-x-6 gap-y-3 text-sm text-fd-muted-foreground"
          >
            <DocLink href="/introduction">Introduction</DocLink>
            <DocLink href="/quick-start">Quick start</DocLink>
            <DocLink href="/language/00-overview">Language</DocLink>
            <DocLink href="/standard-library/00-overview">Standard Library</DocLink>
            <DocLink href="/cli-and-compiler/00-overview">CLI & compiler</DocLink>
            <DocLink href="/internal-engineering/00-overview">Internal Engineering</DocLink>
          </nav>
        </section>
      </main>
    </HomeLayout>
  );
}

function Feature({ title, body }: { title: string; body: ReactNode }) {
  return (
    <div className="rounded-xl border border-fd-border bg-fd-card p-5 text-left">
      <h2 className="text-base font-semibold text-fd-card-foreground">{title}</h2>
      <p className="mt-2 text-sm leading-6 text-fd-muted-foreground">{body}</p>
    </div>
  );
}

function DocLink({ href, children }: { href: string; children: ReactNode }) {
  return (
    <Link href={href} className="transition-colors hover:text-fd-foreground hover:underline">
      {children}
    </Link>
  );
}
