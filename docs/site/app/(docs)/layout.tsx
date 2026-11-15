import { source } from '@/lib/source';
import { DocsLayout } from 'fumadocs-ui/layouts/glass';
import { baseOptions } from '@/lib/layout.shared';
import { BookOpen, Braces, Rocket } from 'lucide-react';
import type { LayoutTab } from 'fumadocs-ui/layouts/shared';
import type { ReactNode } from 'react';

const norm = (url: string) => (url.length > 1 && url.endsWith('/') ? url.slice(0, -1) : url);

const pages = source.getPages();
const urlsFor = (match: (url: string) => boolean) =>
  new Set(pages.filter((page) => match(norm(page.url))).map((page) => norm(page.url)));

const releasesUrls = urlsFor((url) => url.startsWith('/releases'));
const apiReferenceUrls = urlsFor((url) => url.startsWith('/api-reference'));
const docsUrls = urlsFor((url) => !url.startsWith('/api-reference') && !url.startsWith('/releases'));

const tabs: (LayoutTab & { urls?: Set<string> })[] = [
  {
    title: 'Docs',
    description: 'Guides: language, standard library, CLI & compiler, internal engineering',
    url: '/introduction',
    icon: <BookOpen />,
    urls: docsUrls,
  },
  {
    title: 'API reference',
    description: 'Language APIs, standard library signatures, every error code',
    url: '/api-reference/00-overview',
    icon: <Braces />,
    urls: apiReferenceUrls,
  },
  {
    title: 'Releases',
    description: 'Release notes for published CP3 builds',
    url: '/releases/00-overview',
    icon: <Rocket />,
    urls: releasesUrls,
  },
];

export default function Layout({ children }: { children: ReactNode }) {
  return (
    <DocsLayout tree={source.getPageTree()} {...baseOptions()} tabs={tabs}>
      {children}
    </DocsLayout>
  );
}
