/**
 * The official CP3 wordmark, taken from the VS Code extension's assets
 * (dist/extensions/cp3-vscode/images/cp3-logo.svg). The viewBox is cropped
 * to the wordmark's bounding box, so the mark fills the space instead of
 * floating in a padded square. The C and P strokes use the theme foreground
 * color so the mark works on light and dark surfaces; the "3" keeps the
 * brand purple gradient.
 */
export function Cp3Logo({ className }: { className?: string }) {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      viewBox="192 561 1076 337"
      role="img"
      aria-label="CP3"
      className={className}
    >
      <defs>
        <linearGradient id="cp3-logo-purple" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#8F5BFF" />
          <stop offset="1" stopColor="#7C4DF0" />
        </linearGradient>
      </defs>

      <g transform="translate(-168 294)">
        {/* C */}
        <path
          style={{ fill: 'var(--color-fd-foreground)' }}
          d="M520 330 L808 330 L758 380 Q750 383 740 383 L525 383 Q512 384 500 395 L432 458 Q426 466 426 478 Q428 500 448 501 L775 501 L730 552 Q726 555 718 555 L430 555 Q360 555 360 485 Q360 450 390 425 L470 350 Q490 330 520 330 Z"
        />
        {/* P */}
        <path
          style={{ fill: 'var(--color-fd-foreground)' }}
          d="M870 330 L1120 330 Q1198 335 1198 408 Q1196 480 1120 485 L886 485 L886 575 L823 604 L823 432 L1115 432 Q1140 430 1140 408 Q1140 385 1115 383 L815 383 Z"
        />
        {/* 3 */}
        <path
          fill="url(#cp3-logo-purple)"
          d="M1228 300 L1265 267 L1410 267 Q1436 267 1436 295 Q1432 320 1412 338 Q1430 345 1428 365 Q1425 385 1395 400 L1382 410 L1218 410 L1243 377 L1370 377 Q1385 377 1385 366 Q1385 352 1370 352 L1257 352 L1277 324 L1370 324 Q1385 324 1385 313 Q1385 300 1370 300 Z"
        />
      </g>
    </svg>
  );
}
