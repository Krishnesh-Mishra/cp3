import { source } from '@/lib/source';
import type { MetadataRoute } from 'next';

export const dynamic = 'force-static';

const siteUrl = 'https://krishnesh-mishra.github.io/cp3';

export default function sitemap(): MetadataRoute.Sitemap {
  const urls = ['/', ...source.getPages().map((page) => page.url)];

  return urls.map((path) => ({
    url: `${siteUrl}${path === '/' ? '/' : path}`,
    changeFrequency: 'weekly',
    priority: path === '/' ? 1 : 0.5,
  }));
}
