// =============================================================================
//  core/Result.hpp — uniform error channel for the whole application
// -----------------------------------------------------------------------------
//  The brief asks for C++20 *and* std::expected. std::expected is a C++23
//  library feature (P0323R12); no conforming C++20 standard library provides it
//  (verified: g++ 13 rejects <expected> under -std=c++20, accepts it under
//  -std=c++23). We therefore alias to std::expected when the toolchain exposes
//  it and fall back to a minimal, API-compatible implementation otherwise.
//  Application code only ever names core::Result / core::Err, so the day the
//  whole fleet moves to C++23 this header shrinks and nothing else changes.
// =============================================================================
#pragma once

#include <version>   // must come first: it is what defines __cpp_lib_expected

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <cstdint>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
#  define XPG_HAS_STD_EXPECTED 1
#else
#  define XPG_HAS_STD_EXPECTED 0
#endif

namespace core {

// ---------------------------------------------------------------------------
// Error domain. Flat enum + context string: cheap to copy, trivially loggable,
// and mappable to a localized message table for the diagnostics panel.
// ---------------------------------------------------------------------------
enum class ErrorCode : std::uint16_t {
    None = 0,
    // I/O
    FileNotFound, FileUnreadable, FileTooLarge, FileEmpty,
    // Parsing
    XmlMalformed, XmlUnexpectedRoot, XmlMissingAttribute, XmlUnsupportedDtd,
    // Domain
    UnknownCpuReference, DuplicateSymbol, UnresolvedType, IncompleteProject,
    // Runtime
    SdlInit, SdlRenderer, SdlTexture, FontLoad,
    // Programming errors surfaced as values rather than as UB
    InvalidArgument, OutOfRange, NotImplemented, Cancelled,
};

struct Error {
    ErrorCode   code{ErrorCode::None};
    std::string context;   // "line 4127: attribute 'typeName' missing on <variables>"
    std::string source;    // originating file or subsystem

    Error() = default;
    Error(ErrorCode c, std::string ctx = {}, std::string src = {})
        : code(c), context(std::move(ctx)), source(std::move(src)) {}

    [[nodiscard]] std::string message() const;   // defined in Error.cpp
};

// ---------------------------------------------------------------------------
#if XPG_HAS_STD_EXPECTED

template <class T, class E = Error> using Result = std::expected<T, E>;
template <class E = Error>          using Err    = std::unexpected<E>;

#else  // ---------------- C++20 fallback ------------------------------------

template <class E>
class Err {
public:
    explicit Err(E e) : value_(std::move(e)) {}
    E&       error() &       noexcept { return value_; }
    const E& error() const & noexcept { return value_; }
    E&&      error() &&      noexcept { return std::move(value_); }
private:
    E value_;
};
template <class E> Err(E) -> Err<E>;

struct Void {};   // stands in for expected<void, E>

template <class T, class E = Error>
class Result {
    using Stored = std::conditional_t<std::is_void_v<T>, Void, T>;
public:
    using value_type = T;
    using error_type = E;

    Result() : slot_(Stored{}) {}
    Result(Stored v) : slot_(std::move(v)) {}                        // NOLINT: implicit by design
    template <class G> Result(Err<G> e) : slot_(E(std::move(e).error())) {}

    [[nodiscard]] bool has_value() const noexcept { return slot_.index() == 0; }
    explicit operator bool()      const noexcept { return has_value(); }

    Stored&       value() &        { ensure(); return std::get<0>(slot_); }
    const Stored& value() const &  { ensure(); return std::get<0>(slot_); }
    Stored&&      value() &&       { ensure(); return std::get<0>(std::move(slot_)); }

    Stored&       operator*() &       noexcept { return std::get<0>(slot_); }
    const Stored& operator*() const & noexcept { return std::get<0>(slot_); }
    Stored&&      operator*() &&      noexcept { return std::get<0>(std::move(slot_)); }
    Stored*       operator->()        noexcept { return &std::get<0>(slot_); }
    const Stored* operator->()  const noexcept { return &std::get<0>(slot_); }

    const E& error() const & noexcept { return std::get<1>(slot_); }
    E&&      error() &&      noexcept { return std::get<1>(std::move(slot_)); }

    template <class U>
    Stored value_or(U&& fallback) const & {
        return has_value() ? std::get<0>(slot_) : static_cast<Stored>(std::forward<U>(fallback));
    }
private:
    void ensure() const { if (!has_value()) throw std::runtime_error("Result::value() on error"); }
    std::variant<Stored, E> slot_;
};

#endif // XPG_HAS_STD_EXPECTED

// A Result carrying no payload.
using Status = Result<void, Error>;

#if XPG_HAS_STD_EXPECTED
inline Status ok() { return Status{}; }
#else
inline Status ok() { return Status{Void{}}; }
#endif

inline Err<Error> fail(ErrorCode c, std::string ctx = {}, std::string src = {}) {
    return Err<Error>(Error{c, std::move(ctx), std::move(src)});
}

// Propagate on error, bind on success. Used pervasively in the import pipeline.
#define XPG_CAT_(a, b) a##b
#define XPG_CAT(a, b)  XPG_CAT_(a, b)
#define XPG_TRY(decl, expr)                                                        \
    auto&& XPG_CAT(_r_, __LINE__) = (expr);                                        \
    if (!XPG_CAT(_r_, __LINE__))                                                   \
        return ::core::Err<::core::Error>(XPG_CAT(_r_, __LINE__).error());         \
    decl = *std::move(XPG_CAT(_r_, __LINE__))

} // namespace core
