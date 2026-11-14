import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createMDX } from 'fumadocs-mdx/next';

/**
 * The site is a static export deployed to GitHub Pages as a project site
 * (https://krishnesh-mishra.github.io/cp3/), so every asset and link lives
 * under /cp3. `basePath` is wired into next/link automatically; client-side
 * fetches that build URLs by hand read NEXT_PUBLIC_BASE_PATH instead.
 */
const basePath = '/cp3';
const siteDir = path.dirname(fileURLToPath(import.meta.url));

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  output: 'export',
  reactStrictMode: true,
  basePath,
  trailingSlash: true,
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
