// =============================================================================
//  project/Security.hpp — project states and the open lock
// -----------------------------------------------------------------------------
//  WHAT THIS IS, AND WHAT IT IS NOT.
//
//  This is an ACCESS CONTROL, not confidentiality. A locked project refuses to
//  open in this application; the files in the folder remain plain text and
//  anyone with the folder can read them in Notepad. That is what was asked for
//  ("bloquer l'ouverture du projet dans le programme") and it is worth being
//  explicit about, because a lock icon invites people to assume more.
//
//  If real confidentiality is wanted later, the place to add it is here, and the
//  answer is AES-GCM through Windows CNG (bcrypt.h) - a system API, no new
//  dependency - encrypting the folder contents. It is not to hand-roll a cipher.
//
//  WHAT IS IMPLEMENTED
//
//    * SHA-256, written out in full. A hash is safe to implement: it is fully
//      specified, has published test vectors, and there is no key to leak.
//    * Passwords are stored as salt + 200 000 iterations of SHA-256, never in
//      clear. A stolen project.lock does not hand over the password.
//    * The master key is derived from a MAC address entered once, on first run.
//      It is a recovery path for a forgotten password, not a security boundary:
//      a MAC address is readable and spoofable by anyone who cares. Said plainly
//      here so nobody mistakes it for one.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace project {

// NEW    a project created but not yet worked on
// DEV    under active modification, the normal state
// FINISH declared complete; opens normally but the UI warns before editing
// LOCK   refuses to open without the password or the master key
enum class State : std::uint8_t { New, Dev, Finish, Lock };

[[nodiscard]] std::string_view toString(State) noexcept;
[[nodiscard]] State            stateFromString(std::string_view) noexcept;

// --- SHA-256 ---------------------------------------------------------------
using Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Digest      sha256(std::string_view data);
[[nodiscard]] std::string toHex(const Digest&);
[[nodiscard]] bool        fromHex(std::string_view, Digest&);

// Salt + iterated SHA-256. Not PBKDF2-HMAC to the letter, and named for what it
// is rather than borrowing a standard's name it does not implement.
[[nodiscard]] std::string deriveKey(std::string_view secret, std::string_view salt,
                                    int iterations = 200000);
[[nodiscard]] std::string makeSalt();

// --- the lock record -------------------------------------------------------
struct LockRecord {
    std::string salt;
    std::string passwordHash;    // deriveKey(password, salt)
    std::string masterHash;      // deriveKey(masterKey, salt), for recovery
    int         iterations{200000};

    [[nodiscard]] bool valid() const { return !salt.empty() && !passwordHash.empty(); }
};

[[nodiscard]] LockRecord makeLock(std::string_view password, std::string_view masterKey);
[[nodiscard]] bool       checkPassword(const LockRecord&, std::string_view password);
[[nodiscard]] bool       checkMaster(const LockRecord&, std::string_view masterKey);

// --- the master key --------------------------------------------------------
// Stored once, in the application settings, as a hash: the MAC itself is never
// written back to disk in clear, so a settings file does not disclose it.
class MasterKey {
public:
    [[nodiscard]] static std::string normaliseMac(std::string_view mac);
    [[nodiscard]] static bool        looksLikeMac(std::string_view mac);

    void setFromMac(std::string_view mac);
    [[nodiscard]] bool defined() const noexcept { return !stored_.empty(); }
    [[nodiscard]] const std::string& stored() const noexcept { return stored_; }
    void loadStored(std::string stored) { stored_ = std::move(stored); }
    [[nodiscard]] bool matches(std::string_view mac) const;

private:
    std::string stored_;         // deriveKey(normalised mac, fixed salt)
};

} // namespace project
