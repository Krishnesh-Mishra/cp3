// =============================================================================
// src/std/arrays/sema_docs.cpp - the documentation of the array method surface.
//
// OWNERSHIP: the std/arrays family (src/std/arrays/**). The member TYPING of
// the array surface lives in the compiler core (`arrayMember`,
// src/sema/src/checks/member_check.cpp) because the element type follows the
// receiver; the DOCUMENTATION lives here, with the family, so the editor can
// describe every array member the compiler knows.
//
// The descriptions are the documented ones
// (docs/language-spec/09-arrays-strings.md, "Array" section): the exact list
// of methods, what each one does, and the C++ standard library call it is
// backed by.
// =============================================================================
#include "detail/std_tables.hpp"

#include <string>
#include <vector>

namespace cp3 {

namespace {

const DocRow kArrayDocRows[] = {
    {"length", "length: number", "The number of elements in the array."},
    {"push", "push(...items: T[]): number",
     "Appends the items to the end of the array and returns the new length."},
    {"unshift", "unshift(...items: T[]): number",
     "Inserts the items at the front of the array and returns the new "
     "length."},
    {"pop", "pop(): T | undefined",
     "Removes the last element and returns it, or `undefined` when the array "
     "is empty."},
    {"shift", "shift(): T | undefined",
     "Removes the first element and returns it, or `undefined` when the array "
     "is empty."},
    {"join", "join(separator?: string): string",
     "Joins the elements into one string, separated by `separator` (a comma "
     "by default). Backed by `fmt::join`."},
    {"sort", "sort(comparator?: (a: T, b: T) => number): T[]",
     "Sorts the array in place and returns it.\n\n"
     "Without a comparator the ordering is type-aware - a `number[]` sorts "
     "numerically and a `string[]` lexicographically - not JS's "
     "string-conversion default. The sort is stable, so equal elements keep "
     "their relative order. A comparator returns a JS-style number "
     "(negative, zero or positive). Backed by `std::stable_sort`."},
    {"includes", "includes(element: T, fromIndex?: number): boolean",
     "True when the array contains `element`. Backed by `std::find`."},
    {"filter", "filter(predicate: (element: T, index: number, array: T[]) => boolean): T[]",
     "Returns a new array with the elements the predicate accepts."},
    {"find", "find(predicate: (element: T, index: number, array: T[]) => boolean): T | undefined",
     "Returns the first element the predicate accepts, or `undefined` when "
     "there is none."},
    {"findLast", "findLast(predicate: (element: T, index: number, array: T[]) => boolean): T | undefined",
     "Returns the last element the predicate accepts, or `undefined` when "
     "there is none."},
    {"findIndex", "findIndex(predicate: (element: T, index: number, array: T[]) => boolean): number",
     "Returns the index of the first element the predicate accepts, or `-1` "
     "when there is none. Backed by `std::find_if`."},
    {"findLastIndex", "findLastIndex(predicate: (element: T, index: number, array: T[]) => boolean): number",
     "Returns the index of the last element the predicate accepts, or `-1` "
     "when there is none."},
    {"some", "some(predicate: (element: T, index: number, array: T[]) => boolean): boolean",
     "True when the predicate accepts at least one element. Backed by "
     "`std::any_of`."},
    {"every", "every(predicate: (element: T, index: number, array: T[]) => boolean): boolean",
     "True when the predicate accepts every element. Backed by "
     "`std::all_of`."},
    {"map", "map<U>(transform: (element: T, index: number, array: T[]) => U): U[]",
     "Returns a new array with the result of `transform` for every element."},
    {"flatMap", "flatMap<U>(transform: (element: T, index: number, array: T[]) => U[]): U[]",
     "Maps every element and flattens the returned arrays by one level."},
    {"forEach", "forEach(callback: (element: T, index: number, array: T[]) => void): void",
     "Calls the callback for every element in order. The return value is "
     "ignored."},
    {"reduce", "reduce<U>(combine: (accumulator: U, element: T, index: number, array: T[]) => U, initial?: U): U",
     "Folds the array into a single value, calling `combine` for every "
     "element. Without `initial` the first element is the starting value."},
    {"indexOf", "indexOf(element: T, fromIndex?: number): number",
     "Returns the index of the first occurrence of `element`, or `-1` when "
     "the array does not contain it. Backed by `std::find`."},
    {"binarySearch", "binarySearch(target: T): number",
     "Returns the index of `target` in a **sorted** array, or `-1` when it is "
     "not there. Backed by `std::lower_bound`.\n\n"
     "The array has to be sorted already - an unsorted array gives a wrong "
     "answer rather than an error, exactly like the C++ call it is backed "
     "by."},
    {"slice", "slice(start?: number, end?: number): T[]",
     "Returns a new array with the elements from `start` up to (but not "
     "including) `end`."},
    {"at", "at(index: number): T | undefined",
     "Returns the element at `index`; a negative index counts from the end, "
     "and an out-of-range index returns `undefined`."},
    {"toSorted", "toSorted(comparator?: (a: T, b: T) => number): T[]",
     "Returns a sorted copy and leaves the receiver unchanged. Uses the same "
     "type-aware default ordering and stable comparator contract as `sort`."},
    {"toReversed", "toReversed(): T[]",
     "Returns a reversed copy and leaves the receiver unchanged."},
    {"flat", "flat(): T[]",
     "Returns a new array with the nested arrays of the receiver flattened "
     "by one level."},
    {"unique", "unique(): T[]",
     "Removes the adjacent duplicates in place and returns the array. Backed "
     "by `std::unique`."},
    {"shuffle", "shuffle(): T[]",
     "Shuffles the array in place with a seeded random number generator and "
     "returns it. Backed by `std::shuffle`."},
};

}  // namespace

StdMemberDoc arrayMemberDoc(const std::string& prop) {
    return memberDoc(kArrayDocRows, prop);
}

std::vector<std::string> arrayMemberNames() {
    return docRowNames(kArrayDocRows);
}

}  // namespace cp3
