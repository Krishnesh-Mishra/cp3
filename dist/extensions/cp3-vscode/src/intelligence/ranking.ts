// dist/extensions/cp3-vscode/src/intelligence/ranking.ts
//
// Deterministic ranking for completion candidates. The score is a small
// integer; Candidates are ordered by it and turned into a zero-padded
// `sortText`, so the ordering is stable across requests and providers.
//
// Layers (each documented where used):
//   matchRank    - how well the query matches the candidate (exact beats
//                  prefix beats substring beats fuzzy subsequence);
//   corpusBonus  - how often the candidate actually appears in this
//                  workspace's indexed code;
//   pairBonus    - how often the candidate follows the token right before
//                  the cursor (a unigram/bigram language model, learned from
//                  the corpus, not hardcoded).

export type MatchRank =
    | { readonly ok: true; readonly rank: number }
    | { readonly ok: false };

function isBoundary(text: string, index: number): boolean {
    if (index === 0) return true;
    const prev = text[index - 1];
    const cur = text[index];
    if (prev === "_" || prev === "$" || prev === ".") return isWordStart(cur);
    if (isLower(prev) && isUpper(cur)) return true;   // camelCase hump
    if (!isWordStart(prev) && isWordStart(cur)) return true;
    return false;
}

function isWordStart(ch: string): boolean {
    return /[A-Za-z_$]/.test(ch);
}

function isUpper(ch: string): boolean {
    return ch >= "A" && ch <= "Z";
}

function isLower(ch: string): boolean {
    return ch >= "a" && ch <= "z";
}

/**
 * Classifies how `query` matches `candidate`.
 *
 *   0xx - exact (case-sensitive match)
 *   1xx - prefix (candidate starts with query, any case)
 *   2xx - camelCase hump (query matches the word-initial letters)
 *   3xx - substring
 *   4xx - fuzzy subsequence (every query char appears, in order)
 *
 * The tail digits encode "prefer shorter candidates / closer matches".
 */
export function matchRank(query: string, candidate: string): MatchRank {
    if (query.length === 0) {
        return { ok: true, rank: 450 };
    }
    const queryLower = query.toLowerCase();
    const candidateLower = candidate.toLowerCase();

    if (candidate === query) {
        return { ok: true, rank: 0 };
    }
    if (candidateLower.startsWith(queryLower)) {
        // Shorter candidates win inside the prefix bucket.
        return { ok: true, rank: 100 + (candidate.length - query.length) };
    }

    // CamelCase / hump match: query chars must land on word boundaries only.
    if (isHumpMatch(queryLower, candidateLower)) {
        return { ok: true, rank: 200 + (candidate.length - query.length) };
    }

    const substringAt = candidateLower.indexOf(queryLower);
    if (substringAt >= 0) {
        return { ok: true, rank: 300 + substringAt * 4 + (candidate.length - query.length) };
    }

    const fuzzy = fuzzySubsequence(queryLower, candidateLower);
    if (fuzzy !== undefined) {
        return { ok: true, rank: 400 + fuzzy };
    }
    return { ok: false };
}

function isHumpMatch(query: string, candidate: string): boolean {
    let cursor = 0;
    for (const ch of query) {
        let found = -1;
        while (cursor < candidate.length) {
            if (candidate[cursor] === ch && isBoundary(candidate, cursor)) {
                found = cursor;
                break;
            }
            cursor += 1;
        }
        if (found === -1) return false;
        cursor = found + 1;
    }
    return true;
}

function fuzzySubsequence(query: string, candidate: string): number | undefined {
    let cursor = 0;
    let span = 0;
    for (const ch of query) {
        const found = candidate.indexOf(ch, cursor);
        if (found === -1) return undefined;
        span += found - cursor + 1;
        cursor = found + 1;
    }
    return Math.min(99, span);
}

export interface ScoreInput {
    readonly query: string;
    readonly candidate: string;
    /** Corpus occurrences of the candidate (unigram). */
    readonly corpusUsage: number;
    /** Corpus occurrences of (previous token -> candidate). */
    readonly pairUsage: number;
    /** Static prior for candidates with no corpus evidence (higher = shown earlier). 0-40. */
    readonly staticPrior?: number;
}

/**
 * Composite score; lower sorts earlier. Multiplying the match rank keeps the
 * match class dominant — a perfect corpus hit can reorder within a class
 * (sin above asin for query "si") but never promote a substring above a
 * prefix.
 */
export function compositeScore(input: ScoreInput): number | undefined {
    const matched = matchRank(input.query, input.candidate);
    if (!matched.ok) return undefined;
    const corpus = Math.min(80, Math.floor(Math.log10(1 + input.corpusUsage) * 26));
    const pair = Math.min(60, Math.floor(Math.log10(1 + input.pairUsage) * 30));
    const prior = Math.min(40, input.staticPrior ?? 0);
    return matched.rank + 200 - corpus - pair - prior;
}

export function sortTextFor(score: number): string {
    const clamped = Math.max(0, Math.min(99999, score));
    return String(clamped).padStart(5, "0");
}
