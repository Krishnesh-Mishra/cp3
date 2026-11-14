import { loader } from 'fumadocs-core/source';
import { defineDocs } from 'fumadocs-mdx/macro';
import { metaSchema, pageSchema } from 'fumadocs-core/source/schema';
import { createElement } from 'react';
import { Code2, Lightbulb, Rocket } from 'lucide-react';

// the app lives in `docs/site/`; the markdown sources stay in `docs/content/`
const docs = defineDocs({
  dir: '../content',
  docs: {
    schema: pageSchema,
  },
  meta: {
    schema: metaSchema,
  },
});

// icons are referenced by name in page frontmatter (`icon: Rocket`);
// fumadocs itself doesn't bundle an icon set, so map names here
const pageIcons = { Lightbulb, Rocket, Code2 } as const;

/**
 * The documentation lives at the site root (baseUrl '/'), the same layout the
 * previous MkDocs site used, so existing URLs keep working.
 */
export const source = loader({
  baseUrl: '/',
  source: docs.toFumadocsSource(),
  icon: (icon) => {
    const Icon = icon ? pageIcons[icon as keyof typeof pageIcons] : undefined;
    return Icon ? createElement(Icon) : undefined;
  },
});
