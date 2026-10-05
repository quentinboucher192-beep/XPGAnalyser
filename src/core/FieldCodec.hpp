// =============================================================================
//  core/FieldCodec.hpp — packing a form's values into one string
// -----------------------------------------------------------------------------
//  A dialog hands its result back as a single string, and the caller splits it
//  into fields again. That was done with a newline, which worked for as long as
//  every field held a value.
//
//  It stopped working the moment a field held CODE. An action body or a ST
//  expression contains newlines by definition, so a four-field form came back as
//  eleven fields, the caller's `values.size() < 4` check fired, and the edit was
//  silently dropped - the dialog closed, nothing was written, and nothing said
//  why. That is the worst shape a bug can have: it looks like the button did
//  nothing.
//
//  The separator is now US (0x1F, "unit separator"), which exists for exactly
//  this and cannot occur in ST source. It lives here, away from the widgets, so
//  a test can hold it to the one property that matters: WHATEVER goes in comes
//  back out, field for field, however many newlines it contains.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace core {

    // ASCII 0x1F. Not a newline, not a tab, not anything a person can type into a
    // code editor by accident.
    inline constexpr char kFieldSeparator = '\x1f';

    [[nodiscard]] inline std::string joinFields(const std::vector<std::string>& fields) {
        std::string out;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) out.push_back(kFieldSeparator);
            out += fields[i];
        }
        return out;
    }

    // ONE AMBIGUITY, AND IT IS INHERENT. A delimiter-only encoding cannot tell a
    // form with no fields from a form with one empty field: both are the empty
    // string. splitFields resolves it as ONE EMPTY FIELD, because a form with no
    // fields has nothing to return and never asks - while a form whose only field
    // was left blank is an ordinary thing that must come back as a blank field
    // rather than as nothing at all.
    [[nodiscard]] inline std::vector<std::string> splitFields(const std::string& payload) {
        std::vector<std::string> out;
        std::size_t from = 0;
        while (true) {
            const auto at = payload.find(kFieldSeparator, from);
            out.push_back(payload.substr(from, (at == std::string::npos ? payload.size() : at) - from));
            if (at == std::string::npos) break;
            from = at + 1;
        }
        return out;
    }

} // namespace core