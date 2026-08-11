#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem> // dup_utf8's path overload
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

/** @file
    The **wire contract** of the C#/.NET backend: the handful of by-value structs
    the `extern "C"` signatures spell, the error taxonomy every thunk reports
    through, and the catch boundary that guarantees a C++ exception never unwinds
    across the C ABI.

    The four wire structs are declared at **global scope** — they are the types
    the generated `extern "C"` declarations name, and they are mirrored
    byte-for-byte on the managed side (`WelderError`, `WelderOptWire`,
    `WelderSpWire`, `WelderSeqWire` in the generated `Bindings.cs`). Everything
    else lives in `welder::rods::csharp::shim`.

    Wire conversion rules:
    - scalars/bool cross by value (fixed-width spellings, byte-for-byte);
    - a welded enum crosses as its underlying value;
    - strings cross as UTF-8 (`const char*` in; a malloc'd buffer out, freed by
      the managed side via the emitted `welder_free`);
    - a welded class crosses as an opaque handle; a class-typed **return**
      (value or lvalue reference) is heap-copied into a fresh owned handle —
      exactly pybind11's `automatic` behavior for those categories.
*/

/** The C-ABI error slot every generated thunk takes as its trailing out-param.
    `code` `0` means success; `message` is a malloc'd UTF-8 buffer the managed
    side reads and frees via `welder_free` (null when absent). Defined at global
    scope — it is the wire contract type the `extern "C"` signatures spell. */
struct welder_error {
    std::int32_t code;
    char* message;
};

/** The by-value wire form of a `std::optional` with a LEAF payload: `has` plus
    the payload in the kind-matching field (ints/bools/enums in `i`, floats in
    `f`, a malloc'd UTF-8 buffer in `s`, an object pointer in `p`). Mirrored
    managed-side as the blittable `WelderOptWire`. */
struct welder_opt_wire {
    std::uint8_t has;
    std::int64_t i;
    double f;
    const char* s;
    void* p;
};

/** The by-value wire form of a `shared_ptr` RETURN: the object pointer plus
    the boxed `shared_ptr` copy keeping it alive (freed managed-side through
    the per-class `welder_sp_*_free` thunk). Both null for an empty pointer. */
struct welder_sp_wire {
    void* obj;
    void* box;
};

/** The by-value wire form of a scalar/enum sequence: an element-typed buffer +
    length. Returns malloc the buffer (freed managed-side via `welder_free`);
    parameters point at the managed array, pinned for the call. */
struct welder_seq_wire {
    void* data;
    std::int64_t len;
};

namespace welder::inline v0::rods::csharp::shim {

/** The error codes the catch chain writes, mirrored by the managed wrapper's
    `WelderInterop.ThrowIfError` (which maps 2–5 to the matching BCL exception
    types and everything else to `WelderNativeException`). Code 7 is reserved
    for a managed-origin exception round-tripping through a director callback
    (the virtuals phase). */
enum class error_code : std::int32_t {
    none = 0,             /**< Success. */
    std_exception = 1,    /**< A plain `std::exception` — `message` = `what()`. */
    bad_alloc = 2,        /**< `std::bad_alloc` → `OutOfMemoryException`. */
    invalid_argument = 3, /**< `std::invalid_argument` → `ArgumentException`. */
    out_of_range = 4,     /**< `std::out_of_range` → `ArgumentOutOfRangeException`. */
    arithmetic = 5,       /**< overflow/underflow/range → `ArithmeticException`. */
    unknown = 6,          /**< A non-`std::exception` throw (`catch (...)`). */
    managed = 7,          /**< Reserved: a managed exception crossing back. */
};

/** Duplicate @a s into a malloc'd, NUL-terminated UTF-8 buffer the managed side
    frees via the emitted `welder_free`.
    @param s the text to copy.
    @return the malloc'd buffer (null only on allocation failure). */
inline char* dup(std::string_view s) noexcept {
    char* p{static_cast<char*>(std::malloc(s.size() + 1))};
    if (p) {
        std::memcpy(p, s.data(), s.size());
        p[s.size()] = '\0';
    }
    return p;
}

/** Duplicate a `utf8_string`-kind C++ value into the malloc'd wire buffer.

    Overloaded rather than folded into @ref dup because the family is not one
    type: `std::string` / `std::string_view` / `char*` all convert to
    `std::string_view` directly, while a `std::filesystem::path` does not — it
    must be *asked* for its text. `u8string()` is the portable UTF-8 spelling
    (POSIX `native()` is already UTF-8, but Windows' is UTF-16 and `string()`
    would narrow it through the active code page, mangling non-ASCII paths); the
    `char8_t` buffer is byte-identical to the `char` one the wire carries.
    One constrained template rather than two overloads: `std::string` converts to
    BOTH `std::string_view` and `std::filesystem::path`, so an overload pair is
    ambiguous for the commonest case.
    @tparam S the C++ type of the value (deduced).
    @param s  the value to marshal out.
    @return the malloc'd UTF-8 buffer. */
template <class S>
inline char* dup_utf8(const S& s) noexcept {
    if constexpr (std::is_same_v<std::remove_cvref_t<S>, std::filesystem::path>) {
        const std::u8string u8{s.u8string()};
        return dup(std::string_view{reinterpret_cast<const char*>(u8.data()),
                                    u8.size()});
    } else {
        return dup(std::string_view{s});
    }
}

/** Reset @a err to success (every thunk's first act).
    @param err the error slot (may be null). */
inline void clear(welder_error* err) noexcept {
    if (err) {
        err->code = 0;
        err->message = nullptr;
    }
}

/** Record a failure in @a err.
    @param err  the error slot (may be null).
    @param code the taxonomy code.
    @param msg  the message text (copied into a malloc'd buffer). */
inline void set_error(welder_error* err, error_code code,
                      std::string_view msg) noexcept {
    if (err) {
        err->code = static_cast<std::int32_t>(code);
        err->message = dup(msg);
    }
}

/** A managed (C#) exception crossing BACK through a director callback: the
    override's error slot re-throws as this type, so when it reaches the next
    thunk boundary the catch chain records it as @ref error_code::managed and
    the managed side rethrows the original message. */
struct managed_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/** Rethrow a director callback's failed error slot as @ref managed_exception
    (frees the slot's message buffer).
    @param e the callback's error slot. */
[[noreturn]] inline void rethrow_managed(welder_error* e) {
    std::string msg{e && e->message ? e->message : "managed exception"};
    if (e && e->message)
        std::free(e->message);
    throw managed_exception{msg};
}

/** THE catch chain — every thunk's exception boundary in one place: clear
    @a err, run @a f, translate any escaping C++ exception into the error
    taxonomy (never letting it unwind through the C ABI), and return @a Wire{}
    on failure. The taxonomy catches the std hierarchy most-derived-first.
    @tparam Wire the thunk's wire return type (possibly `void`).
    @tparam F    the callable's type (deduced).
    @param err the error slot to report through.
    @param f   the work to run under the boundary.
    @return @a f's result, or a value-initialized @a Wire on failure. */
template <class Wire, class F>
Wire caught(welder_error* err, F&& f) noexcept {
    clear(err);
    try {
        if constexpr (std::is_void_v<Wire>)
            f();
        else
            return f();
    } catch (const managed_exception& e) {
        set_error(err, error_code::managed, e.what());
    } catch (const std::bad_alloc& e) {
        set_error(err, error_code::bad_alloc, e.what());
    } catch (const std::invalid_argument& e) {
        set_error(err, error_code::invalid_argument, e.what());
    } catch (const std::out_of_range& e) {
        set_error(err, error_code::out_of_range, e.what());
    } catch (const std::overflow_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::underflow_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::range_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    if constexpr (!std::is_void_v<Wire>)
        return Wire{};
}

} // namespace welder::inline v0::rods::csharp::shim
