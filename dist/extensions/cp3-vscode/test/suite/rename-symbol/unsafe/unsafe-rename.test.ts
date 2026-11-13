// Rename symbol / unsafe positions and invalid new names
import { renameCases } from "../../harness";

renameCases("Rename symbol: unsafe positions and invalid names", [
    {
        name: "a rename is offered for a local variable even when the name is not exported",
        source: "const value = 1;\nconst read = __CURSOR__value;\n",
        newName: "movedValue",
    },
    {
        name: "renaming a reserved keyword position yields no edits",
        source: "__CURSOR__const value = 1;\n",
        newName: "renamed",
        expectFailure: true,
    },
    {
        name: "renaming to the reserved word 'any' is rejected",
        source: "const target = 1;\nconst read = __CURSOR__target;\n",
        newName: "any",
        expectFailure: true,
    },
    {
        name: "renaming to the reserved word 'yield' is rejected",
        source: "const target = 1;\nconst read = __CURSOR__target;\n",
        newName: "yield",
        expectFailure: true,
    },
    {
        name: "renaming to the reserved word 'delete' is rejected",
        source: "const target = 1;\nconst read = __CURSOR__target;\n",
        newName: "delete",
        expectFailure: true,
    },
    {
        name: "renaming a type alias to a primitive type name is allowed or rejected consistently",
        source: "type Radians = number;\nconst value: __CURSOR__Radians = 1;\n",
        newName: "Angle",
    },
    {
        name: "renaming an identifier that starts a line to an empty-ish invalid token is rejected",
        source: "const value = 1;\nconst read = __CURSOR__value;\n",
        newName: "not a valid identifier",
        expectFailure: true,
    },
    {
        name: "renaming to a name that is a valid cp3 identifier works",
        source: "const value = 1;\nconst read = __CURSOR__value;\n",
        newName: "totallyNewName1",
        expectEditCount: 2,
    },
    {
        name: "renaming a marker inside a string literal is not a symbol",
        source: "const target = 1;\nconst label = \"__CURSOR__target\";\n",
        newName: "renamed",
        expectFailure: true,
    },
    {
        name: "renaming a marker inside a comment is not a symbol",
        source: "const target = 1;\n// __CURSOR__target\nconst read = target;\n",
        newName: "renamed",
        expectFailure: true,
    },
]);
