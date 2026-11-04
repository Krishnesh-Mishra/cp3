# Stage 0 cycle statistics (automatic cycle handling)

- .cp3 files scanned: 2079
- classes/interfaces named: 61
- class fields scanned: 137
- class->class field edges: 29
- self-referential classes: 1
- 2-class cycles (mutual): 2
- longer cycles (>2 classes): 0
- cycle-capable classes: 5
- acyclic classes: 56
- weak field edges already in corpus: 1
- cycle groups with no weak edge (upper bound of step-4 manual weak): 2 of 3 groups

Cycle groups:
- A (tests\cases\memory-weak\_ambiguous\unbroken\unbroken_cycle_warning_text\main.cp3), B (tests\cases\memory-weak\_ambiguous\unbroken\unbroken_cycle_warning_text\main.cp3) [weak: no]
- BST (tests\cases\types\01\nullable\nullable_bst_search\main.cp3) [weak: no]
- Child (tests\cases\classes\03\weak\weak_field_ok\main.cp3), Parent (tests\cases\classes\03\weak\weak_field_ok\main.cp3) [weak: yes]
