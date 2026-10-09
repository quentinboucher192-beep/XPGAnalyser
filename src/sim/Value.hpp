// =============================================================================
//  sim/Value.hpp — the values a simulated program manipulates
// -----------------------------------------------------------------------------
//  IEC 61131-3 is statically typed and its arithmetic is not C's: DINT and UDINT
//  wrap at 32 bits, INT at 16, and mixing them is a compile error in Control
//  Expert rather than a silent promotion. The simulator keeps the declared type
//  on every value so that a program which relies on 16-bit wraparound behaves
//  here as it does in the PLC, and so that a type mismatch is reported instead
//  of quietly working.
//
//  TIME is stored in milliseconds, which is what Control Expert's TIME literal
//  resolves to and what its conversions assume.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sim {

enum class Type : std::uint8_t {
    Unknown, Bool, Byte, Word, DWord, Int, DInt, UInt, UDInt, Real, Time, String,
    // 1.12.1 (le dialecte de l'IHM) : des millisecondes sur 64 bits - DATE depuis le
    // 1970-01-01 (a minuit), TIME_OF_DAY depuis minuit, DATE_AND_TIME depuis le
    // 1970-01-01 00:00 (l'heure du mur, sans fuseau). D#2026-10-09, TOD#14:30:00,
    // DT#2026-10-09-14:30:00.
    Date, Tod, Dt,
};

[[nodiscard]] std::string_view toString(Type) noexcept;
[[nodiscard]] Type             typeFromName(std::string_view) noexcept;
[[nodiscard]] bool             isInteger(Type) noexcept;
[[nodiscard]] bool             isNumeric(Type) noexcept;
[[nodiscard]] int              bitWidth(Type) noexcept;
// 1.12.1 : DATE, TIME_OF_DAY, DATE_AND_TIME.
[[nodiscard]] bool             isDateType(Type) noexcept;
// Leur texte : "2026-10-09", "14:30:00" (".250" si des millisecondes), "2026-10-09-14:30:00".
[[nodiscard]] std::string      dateText(Type, std::int64_t milliseconds);
// Le texte d'un litteral, sans son prefixe (2026-10-09 ; 14:30 ; 2026-10-09-14:30:15.5) ;
// faux : illisible (un mois 13, une heure 25...).
bool                           parseDateText(Type, std::string_view text, std::int64_t& milliseconds) noexcept;
// Les jours depuis le 1970-01-01 d'une date du calendrier, et l'inverse.
[[nodiscard]] std::int64_t     daysFromCivil(std::int64_t year, unsigned month, unsigned day) noexcept;
void                           civilFromDays(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) noexcept;

class Value {
public:
    Value() = default;
    static Value boolean(bool v);
    static Value integer(Type t, std::int64_t v);
    static Value real(double v);
    static Value time(std::int64_t milliseconds);
    static Value text(std::string v);
    static Value defaultOf(Type t);

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool isTruthy() const noexcept;

    [[nodiscard]] std::int64_t  asInteger() const noexcept;
    [[nodiscard]] double        asReal() const noexcept;
    [[nodiscard]] const std::string& asString() const noexcept { return text_; }
    [[nodiscard]] std::string   display() const;   // for the watch window

    // Stores `v` into a slot already declared as `type()`, truncating to the
    // declared width the way the PLC does rather than widening the slot.
    void assignFrom(const Value& v);

    [[nodiscard]] bool equals(const Value& other) const;
    [[nodiscard]] int  compare(const Value& other) const;   // <0, 0, >0

private:
    Type         type_{Type::Unknown};
    std::int64_t integer_{0};
    double       real_{0.0};
    std::string  text_;
};

// Wraps `v` to the declared width, signed or unsigned as the type requires.
[[nodiscard]] std::int64_t truncateTo(Type, std::int64_t v);

} // namespace sim
