# Autocomplete / IntelliSense

Target: ~180 cases.

Subcategories: Variable 15, Function 15, Object property 20, Class/interface 15, Import 15, Keyword 10, Type 15, Generic 10, Context-aware 15, Scope/visibility 15, Partial identifier filtering 10, Invalid/incomplete syntax 10, Completion ranking 5, Cursor-position edge cases 10.

Status: 1 case implemented — object-properties.test.ts (the pattern example).

One folder per subcategory (`<subcategory>/`), one file per case: `<subcategory>/<case>.test.ts`, e.g. `variable/const-number-autocomplete.test.ts`.
