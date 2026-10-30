// =============================================================================
// cp3/core/value.hpp - reference model, shared closure cells, weak links, and the
// structured-clone (deep copy) ABI.
//
// OWNERSHIP: runtime module. This header is the *codegen/runtime contract* for
// cp3's memory model (docs/language-spec/10-memory-management.md and
// 11-reference-cycles-the-weak-keyword.md). Generated C++ obeys it; codegen
// emits these spellings.
//
// The value model (must match cp3/types.hpp):
//   * value types      -> plain C++ values, copied on assignment.
//   * reference types  -> `Ref<T>` = `std::shared_ptr<T>` (spec §10/§12),
//                         heap-allocated, atomically refcounted, handle-copied.
//   * `weak` links     -> `WeakRef<T>` = `std::weak_ptr<T>` (spec §11).
//   * captured `let`/`var` (captured *and* reassigned) -> `Cell<T>`, a shared
//                         refcounted box so the enclosing function and every
//                         closure see the same variable (spec §10 "Closures").
//
// Nothing here allocates on its own except through the constructors/makers.
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"

namespace cp3::runtime {

// Reuse shared_ptr control-block storage after the last owner releases it.
// `allocate_shared` still performs the standard atomic reference counting and
// destroys the object at the right time; only the underlying block allocation
// is pooled. Each allocator specialization has a bounded thread-local free
// list, so repeated allocation/destruction cycles can reuse storage without
// contending on a global lock. A block freed on another thread is cached by
// that thread and remains reusable because the free lists are keyed by the
// exact allocator specialization.
template <class T>
class RefPoolAllocator {
    struct FreeNode { FreeNode* next; };
    static constexpr std::size_t kMaxCachedBytes = 2 * 1024 * 1024;
    static constexpr std::size_t kMaxCachedBlocks =
        sizeof(T) >= kMaxCachedBytes ? 1 : kMaxCachedBytes / sizeof(T);

    struct Cache {
        FreeNode* head = nullptr;
        std::size_t size = 0;

        ~Cache() {
            while (head) {
                FreeNode* node = head;
                head = head->next;
                release(node);
            }
        }
    };

    static constexpr bool poolable =
        sizeof(T) >= sizeof(FreeNode) && alignof(T) >= alignof(FreeNode);

    static Cache& cache() {
        thread_local Cache value;
        return value;
    }

    static void* acquire(std::size_t size) {
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return ::operator new(size, std::align_val_t{alignof(T)});
        } else {
            return ::operator new(size);
        }
    }

    static void release(void* ptr) noexcept {
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            ::operator delete(ptr, std::align_val_t{alignof(T)});
        } else {
            ::operator delete(ptr);
        }
    }

public:
    using value_type = T;

    RefPoolAllocator() noexcept = default;
    template <class U>
    RefPoolAllocator(const RefPoolAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t count) {
        if (count == 1) {
            if constexpr (poolable) {
                Cache& local = cache();
                if (local.head) {
                    FreeNode* node = local.head;
                    local.head = node->next;
                    --local.size;
                    return reinterpret_cast<T*>(node);
                }
            }
        }
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        return static_cast<T*>(acquire(count * sizeof(T)));
    }

    void deallocate(T* ptr, std::size_t count) noexcept {
        if (!ptr) return;
        if (count == 1) {
            if constexpr (poolable) {
                Cache& local = cache();
                if (local.size < kMaxCachedBlocks) {
                    auto* node = ::new (static_cast<void*>(ptr)) FreeNode{local.head};
                    local.head = node;
                    ++local.size;
                    return;
                }
            }
        }
        release(ptr);
    }

    template <class U>
    bool operator==(const RefPoolAllocator<U>&) const noexcept { return true; }
    template <class U>
    bool operator!=(const RefPoolAllocator<U>&) const noexcept { return false; }
};

// =============================================================================
// 1. Reference handles
// =============================================================================

// A cp3 reference-type value. `std::shared_ptr` used as-is (spec §12): atomic
// refcounting, no custom smart pointer. Copying a `Ref<T>` copies the handle,
// never the object - the JS sharing behavior spec §8 §10 require.
template <class T>
using Ref = std::shared_ptr<T>;

// The canonical allocation for a reference-type object. Generated code emits
// `makeRef<Obj>(args...)` for `{ ... }` / class instantiation.
template <class T, class... Args>
Ref<T> makeRef(Args&&... args) {
    return std::allocate_shared<T>(RefPoolAllocator<T>{},
                                   std::forward<Args>(args)...);
}

// A reference-counted handle for a compiler-proven thread-confined class.
// It preserves aliasing and destruction semantics while using a plain count;
// the compiler emits it only when no worker/parallel boundary, weak reference,
// inheritance, interface conversion, or exported ABI can expose the handle to
// another thread. Unproven classes continue to use atomic Ref<T>.
template <class T>
class LocalRef {
    struct Control {
        std::size_t owners = 1;
        T value;

        template <class... Args>
        explicit Control(Args&&... args)
            : value(std::forward<Args>(args)...) {}
    };

    Control* control_ = nullptr;

    explicit LocalRef(Control* control) noexcept : control_(control) {}

    void retain() noexcept {
        if (control_) ++control_->owners;
    }

    void release() noexcept {
        Control* old = control_;
        control_ = nullptr;
        if (old && --old->owners == 0) {
            old->~Control();
            RefPoolAllocator<Control>{}.deallocate(old, 1);
        }
    }

    template <class U, class... Args>
    friend LocalRef<U> makeLocalRef(Args&&... args);

public:
    using element_type = T;

    LocalRef() noexcept = default;
    LocalRef(std::nullptr_t) noexcept {}

    LocalRef(const LocalRef& other) noexcept : control_(other.control_) {
        retain();
    }
    LocalRef(LocalRef&& other) noexcept
        : control_(std::exchange(other.control_, nullptr)) {}

    ~LocalRef() { release(); }

    LocalRef& operator=(const LocalRef& other) noexcept {
        if (this == &other) return *this;
        Control* next = other.control_;
        if (next) ++next->owners;
        release();
        control_ = next;
        return *this;
    }
    LocalRef& operator=(LocalRef&& other) noexcept {
        if (this == &other) return *this;
        release();
        control_ = std::exchange(other.control_, nullptr);
        return *this;
    }
    LocalRef& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    T* get() noexcept { return control_ ? &control_->value : nullptr; }
    T* get() const noexcept {
        return control_ ? &control_->value : nullptr;
    }
    T& operator*() noexcept { return control_->value; }
    T& operator*() const noexcept { return control_->value; }
    T* operator->() noexcept { return get(); }
    T* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return control_ != nullptr; }
    std::size_t use_count() const noexcept {
        return control_ ? control_->owners : 0;
    }
    void reset() noexcept { release(); }

    friend bool operator==(const LocalRef& a, const LocalRef& b) noexcept {
        return a.get() == b.get();
    }
    friend bool operator!=(const LocalRef& a, const LocalRef& b) noexcept {
        return !(a == b);
    }
    friend bool operator==(const LocalRef& a, std::nullptr_t) noexcept {
        return !a;
    }
    friend bool operator==(std::nullptr_t, const LocalRef& a) noexcept {
        return !a;
    }
    friend bool operator!=(const LocalRef& a, std::nullptr_t) noexcept {
        return static_cast<bool>(a);
    }
    friend bool operator!=(std::nullptr_t, const LocalRef& a) noexcept {
        return static_cast<bool>(a);
    }
};

template <class T, class... Args>
LocalRef<T> makeLocalRef(Args&&... args) {
    using Control = typename LocalRef<T>::Control;
    RefPoolAllocator<Control> allocator;
    Control* storage = allocator.allocate(1);
    try {
        ::new (static_cast<void*>(storage)) Control(std::forward<Args>(args)...);
    } catch (...) {
        allocator.deallocate(storage, 1);
        throw;
    }
    return LocalRef<T>(storage);
}

// A cp3 array `T[]`. Arrays are reference types (spec §10), so the handle is
// shared exactly like an object handle.
template <class E>
using Array = Ref<std::vector<E>>;

template <class E>
Array<E> makeArray() {
    return makeRef<std::vector<E>>();
}

template <class E>
Array<E> makeArray(std::vector<E> elements) {
    static_assert(std::is_move_constructible<E>::value, "array element must be movable");
    return makeRef<std::vector<E>>(std::move(elements));
}

// =============================================================================
// 2. Weak links (spec §11)
// =============================================================================

// A `weak` link. Mechanically `std::weak_ptr<T>` (spec §11); it does not keep
// its target alive. Reading a weak field whose target is gone yields cp3
// `undefined`, represented as `std::nullopt` in `MaybeRef<T>`.
//
// CODEGEN CONTRACT (weak fields):
//   * a `weak` field is declared with C++ type `cp3::runtime::WeakRef<T>`;
//   * reading it emits `cp3::runtime::readWeak(field)`, whose type is
//     `cp3::runtime::MaybeRef<T>` (= `std::optional<Ref<T>>`) - i.e. the
//     representation of the optional type `T | undefined`;
//   * `nullopt` is `undefined`; a present optional is the live target handle.
//   * assigning emits `field = cp3::runtime::WeakRef<T>(strongHandle)`;
//     assigning cp3 `undefined`/`null` emits `field.reset()`.
template <class T>
class WeakRef {
public:
    using element_type = T;

    WeakRef() noexcept = default;
    WeakRef(std::nullptr_t) noexcept {}
    WeakRef(const Ref<T>& strong) noexcept : ptr_(strong) {}

    WeakRef& operator=(const Ref<T>& strong) noexcept {
        ptr_ = strong;
        return *this;
    }
    WeakRef& operator=(std::nullptr_t) noexcept {
        ptr_.reset();
        return *this;
    }

    // The live target, or an empty handle when it was destroyed.
    Ref<T> lock() const noexcept { return ptr_.lock(); }
    bool expired() const noexcept { return ptr_.expired(); }
    void reset() noexcept { ptr_.reset(); }

    // Raw escape hatch for generated code / interop; not a cp3-visible concept.
    const std::weak_ptr<T>& raw() const noexcept { return ptr_; }

private:
    std::weak_ptr<T> ptr_;
};

// The value of reading a weak link: `Ref<T>`, or `undefined` (nullopt).
template <class T>
using MaybeRef = std::optional<Ref<T>>;

// Reading a `weak` field. Dead target -> `std::nullopt` (cp3 `undefined`).
template <class T>
MaybeRef<T> readWeak(const WeakRef<T>& link) noexcept {
    Ref<T> strong = link.lock();
    if (!strong) {
        return std::nullopt;
    }
    return MaybeRef<T>(std::move(strong));
}

// Copying a `weak` link copies the link (spec §10: handle-copy semantics).
template <class T>
WeakRef<T> cloneWeak(const WeakRef<T>& link) noexcept {
    return WeakRef<T>(link.lock());
}

// =============================================================================
// 3. Shared closure cells (spec §10 "Closures")
// =============================================================================

// The stored representation of a `let`/`var` that is captured by at least one
// closure AND is reassignable: a shared, reference-counted cell. The enclosing
// function and every capturing closure hold the same `Cell<T>` handle, so a
// write through any of them is seen by all of them (real JS semantics).
//
// The cell's own refcount is atomic (`std::shared_ptr`); the payload `T` is an
// ordinary non-atomic value (spec §10 - cells never make the variable atomic).
//
// CODEGEN CONTRACT:
//   * declare the binding as `cp3::runtime::Cell<T> name(...);` when sema marks
//     it "captured and reassignable";
//   * reads emit `name.get()`, writes emit `name.set(value)`;
//   * closures copy the `Cell<T>` handle (cheap) - never `T` itself.
//   * a captured-but-never-reassigned `const` stays a plain value/handle and
//     must NOT use a cell (spec §10: "a closure that captures a const ... simply
//     holds a copy").
template <class T>
class Cell {
public:
    // A cell always holds a value (spec has no uninitialized bindings).
    Cell() : ptr_(makeRef<T>()) {}
    explicit Cell(const T& initial) : ptr_(makeRef<T>(initial)) {}
    explicit Cell(T&& initial) : ptr_(makeRef<T>(std::move(initial))) {}

    T& get() noexcept { return *ptr_; }
    const T& get() const noexcept { return *ptr_; }

    void set(const T& value) { *ptr_ = value; }
    void set(T&& value) { *ptr_ = std::move(value); }

    T& operator*() noexcept { return *ptr_; }
    const T& operator*() const noexcept { return *ptr_; }
    T* operator->() noexcept { return ptr_.get(); }
    const T* operator->() const noexcept { return ptr_.get(); }

    // The underlying handle (for tests/diagnostics and cross-cell aliasing).
    Ref<T> handle() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return static_cast<bool>(ptr_); }

private:
    Ref<T> ptr_;
};

template <class T>
Cell<T> makeCell(T initial) {
    return Cell<T>(std::move(initial));
}

// =============================================================================
// 4. Structured clone / deep copy (the `worker` boundary primitive)
// =============================================================================

// `clone(x)` is the runtime's deep-copy primitive. It is a *customization
// point*: codegen specializes `Cloner<T>` for every generated object/class
// type so that a worker boundary (spec §13: "copied in as a genuine deep copy")
// really copies the graph instead of sharing handles.
//
// Default rule: value types copy. Reference handles deep-copy what they point
// at. Arrays copy element-wise. Cycles are NOT handled by the default rule
// (a cp3 value graph crossing a worker boundary is expected to be a tree; a
// cycle through strong links would recurse - document it and don't build one).
//
// CODEGEN CONTRACT (generated object type `Obj` with fields f0..fn):
//   namespace cp3::runtime {
//     template <> struct Cloner<Obj> {
//       static Obj clone(const Obj& v) { return Obj{ clone(v.f0), ... }; }
//     };
//   }
// or the ADL form: define `Obj cp3Clone(const Obj&)` in the object's own
// namespace; `Cloner<T>`'s default implementation prefers that if it exists.
//
// Declared first: every specialisation below deep-copies *through* the public
// `clone` entry point, which is defined once they all exist.
template <class T>
T clone(const T& value);

template <class T>
struct Cloner {
    static T clone(const T& value) { return value; }
};

// Reference handle: deep-copy the pointee. A null handle clones to null.
template <class T>
struct Cloner<std::shared_ptr<T>> {
    static std::shared_ptr<T> clone(const std::shared_ptr<T>& value) {
        if (!value) {
            return nullptr;
        }
        using Pointee = std::remove_cv_t<T>;
        return makeRef<Pointee>(cp3::runtime::clone(*value));
    }
};

template <class T>
struct Cloner<LocalRef<T>> {
    static LocalRef<T> clone(const LocalRef<T>& value) {
        if (!value) return nullptr;
        return makeLocalRef<T>(cp3::runtime::clone(*value));
    }
};

// Array: element-wise deep copy.
template <class E, class A>
struct Cloner<std::vector<E, A>> {
    static std::vector<E, A> clone(const std::vector<E, A>& value) {
        std::vector<E, A> out;
        out.reserve(value.size());
        for (const E& element : value) {
            out.push_back(cp3::runtime::clone(element));
        }
        return out;
    }
};

// Optional (the `T | undefined` representation of a weak read): clone through.
template <class E>
struct Cloner<std::optional<E>> {
    static std::optional<E> clone(const std::optional<E>& value) {
        if (!value) {
            return std::nullopt;
        }
        return std::optional<E>(cp3::runtime::clone(*value));
    }
};

// Weak links do not cross a structured-clone boundary: the clone is an empty
// link, whose reads are `undefined`. This preserves `worker` isolation (a weak
// link must not smuggle a shared object across the thread boundary).
template <class T>
struct Cloner<WeakRef<T>> {
    static WeakRef<T> clone(const WeakRef<T>&) { return WeakRef<T>(); }
};

// A shared closure cell clones to a *new*, independent cell (worker isolation).
template <class T>
struct Cloner<Cell<T>> {
    static Cell<T> clone(const Cell<T>& value) { return Cell<T>(cp3::runtime::clone(value.get())); }
};

// The public spelling. Generated code calls `cp3::runtime::clone(x)`.
template <class T>
T clone(const T& value) {
    return Cloner<std::remove_cv_t<T>>::clone(value);
}

// =============================================================================
// 5. Nullish helpers (cp3 `null` vs `undefined`)
// =============================================================================

// cp3 `undefined` is `Undefined`; cp3 `null` is `std::nullptr_t` (see
// runtime.hpp and cp3/types.hpp). "Nullish" is exactly those two.
inline constexpr bool isNullish(std::nullptr_t) noexcept { return true; }
inline constexpr bool isNullish(Undefined) noexcept { return true; }
inline constexpr bool isNullish(double) noexcept { return false; }
inline constexpr bool isNullish(const std::string&) noexcept { return false; }
inline constexpr bool isNullish(bool) noexcept { return false; }

// A reference handle is never cp3-nullish (cp3 `null` is its own value type).
template <class T>
inline constexpr bool isNullish(const Ref<T>&) noexcept {
    return false;
}
template <class T>
inline constexpr bool isNullish(const LocalRef<T>&) noexcept {
    return false;
}

// `undefined` for a value slot of type `Ref<T>`/`Cell<T>`/etc. is represented
// by the surrounding optional type (see `MaybeRef`), not by a null handle.

inline constexpr bool isUndefined(Undefined) noexcept { return true; }
inline constexpr bool isUndefined(std::nullptr_t) noexcept { return false; }
template <class T>
inline constexpr bool isUndefined(const T&) noexcept {
    return false;
}

inline constexpr bool isNull(std::nullptr_t) noexcept { return true; }
inline constexpr bool isNull(Undefined) noexcept { return false; }
template <class T>
inline constexpr bool isNull(const T&) noexcept {
    return false;
}

// =============================================================================
// 6. `typeof` (DECISIONS D16)
// =============================================================================
//
// D16: `typeof <expr>` yields one of the strings
// "number" | "string" | "boolean" | "undefined" | "object" | "function".
//
// Notes on the two edges the spec does not spell out (chosen for JS parity):
//   * `typeof null` is "object" (JS parity; the spec only fixes the value set).
//   * a reference-type value (object/array/class instance) is "object";
//     a callable is "function".
// `Overload` below lets codegen pick "function" for closures by calling
// `typeOfFunction(...)` instead of `typeOf(...)`.
inline std::string typeOf(Undefined) { return "undefined"; }
inline std::string typeOf(std::nullptr_t) { return "object"; }
inline std::string typeOf(double) { return "number"; }
inline std::string typeOf(const std::string&) { return "string"; }
inline std::string typeOf(bool) { return "boolean"; }

template <class T>
inline std::string typeOf(const Ref<T>&) {
    return "object";
}

inline std::string typeOfFunction() { return "function"; }

// =============================================================================
// 7. Cell helpers used by codegen
// =============================================================================

// `cellOf(value)` for a plain value.
template <class T>
Cell<T> cellOf(T value) {
    return Cell<T>(std::move(value));
}

}  // namespace cp3::runtime
