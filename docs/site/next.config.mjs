import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createMDX } from 'fumadocs-mdx/next';

/**
 * The site is a static export deployed to GitHub Pages as a project site
 * (https://krishnesh-mishra.github.io/cp3/), so every asset and link lives
 * under `/cp3`. `basePath` is wired into next/link automatically; client-side
 * fetches that build URLs by hand read NEXT_PUBLIC_BASE_PATH instead.
 * Local dev (next dev) serves at /; production builds (next build) use /cp3.
 * Force either with CP3_BASE_PATH (e.g. CP3_BASE_PATH=/cp3 next dev).
 */
const basePath = process.env.CP3_BASE_PATH ?? (process.env.NODE_ENV === 'development' ? '' : '/cp3');
const siteDir = path.dirname(fileURLToPath(import.meta.url));

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  output: 'export',
  reactStrictMode: true,
  basePath,
  trailingSlash: true,
  // don't generate the Next.js agent-rules AGENTS.md on every dev run
  agentRules: false,
  // the docs site is served on 127.0.0.1 locally; allow HMR from it
  allowedDevOrigins: ['127.0.0.1'],
  images: {
    unoptimized: true,
  },
  env: {
    NEXT_PUBLIC_BASE_PATH: basePath,
  },
  turbopack: {
    // the markdown sources live in `docs/content/`, one level above the
    // app in `docs/site/`; allow Turbopack to read them
    root: path.join(siteDir, '..'),
  },
};

export default withMDX(config);
