import { loader } from 'fumadocs-core/source';
import { defineDocs } from 'fumadocs-mdx/macro';
import { metaSchema, pageSchema } from 'fumadocs-core/source/schema';

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

/**
 * The documentation lives at the site root (baseUrl '/'), the same layout the
 * previous MkDocs site used, so existing URLs keep working.
 */
export const source = loader({
  baseUrl: '/',
  source: docs.toFumadocsSource(),
});
