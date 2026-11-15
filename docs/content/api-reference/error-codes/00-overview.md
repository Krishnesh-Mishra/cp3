---
title: Error codes
---
# Error codes

Every `CP`-numbered diagnostic the CP3 compiler can print, grouped by the stage that reports it. Each page shows the trigger, a minimal reproducer, what happens when it fires, and how to fix it — plus the codes it is commonly confused with.

| Range | Group | Stage |
| --- | --- | --- |
| [CP3001](./01-internal/cp3001.md) | Internal | compiler bug; please report |
| [CP3101–CP3107](./10-lexical/cp3101.md) | Lexical | the lexer |
| [CP3201–CP3208](./20-syntax/cp3201.md) | Syntax | the parser |
| [CP3301–CP3306](./30-names/cp3301.md) | Names and resolution | semantic analysis |
| [CP3401–CP3421](./40-types/cp3401.md) | Types | semantic analysis |
| [CP3501–CP3520](./50-statements/cp3501.md) | Statements and control flow | semantic analysis |
| [CP3601–CP3610](./60-modules/cp3601.md) | Modules and native imports | module resolution / sema |
| [CP3701–CP3709](./70-classes/cp3701.md) | Classes and interfaces | semantic analysis |
| [CP3801–CP3808](./80-banned/cp3801.md) | Banned JavaScript patterns | semantic analysis |
| [CP3901–CP3916](./90-concurrency/cp3901.md) | Concurrency | semantic analysis |

Codes are assigned and frozen by family; new codes are appended inside their existing range and never renumbered. A code listed here that your compiler never prints is reserved, not missing.
