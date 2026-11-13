# Diagnostics / Error Detection

Target: ~150 cases.
Subcategories: Syntax 20, Type mismatch 20, Undefined variables 10, Invalid arguments 15, Missing properties 10, Invalid imports 10, Interface violations 10, Class implementation 10, Generic type errors 10, Null/undefined errors 10, Async/Promise 5, Duplicate declarations 5, Error recovery while typing 5, Diagnostic positions and ranges 10.

Status: 0 cases implemented (autocomplete/object-properties.test.ts is the pattern example, counted under autocomplete).


One folder per subcategory (<subcategory>/), one file per case: <subcategory>/<case>.test.ts, e.g. variable/const-number-autocomplete.test.ts.
