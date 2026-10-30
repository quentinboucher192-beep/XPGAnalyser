// =============================================================================
//  app/ai/AiSecret.cpp - voir AiSecret.hpp
// =============================================================================
#include "AiSecret.hpp"

#include "AiPaths.hpp"
#include "../../core/AtomicFile.hpp"

#include <filesystem>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <wincrypt.h>
#    include <dpapi.h>
#endif

namespace app::ai {

namespace fs = std::filesystem;

namespace {

// L'en-tete du fichier : la version du format, puis le bloc protege (binaire).
constexpr std::string_view kMagic = "IHMSTUDIO-CLE1\n";

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::mutex& protectorMutex() {
    static std::mutex m;
    return m;
}
KeyProtector& protectorRef() {
    static KeyProtector p;
    return p;
}

#if defined(_WIN32)
// Une "entropie" propre au logiciel : pas un secret, elle evite qu'un autre programme de la
// meme session relise le bloc sans la connaitre.
constexpr char kEntropy[] = "IHM Studio / Assistant IA / cle API";

std::optional<std::string> dpapiProtect(const std::string& plain) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof kEntropy - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"IHM Studio - cle API", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string r(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return r;
}

std::optional<std::string> dpapiUnprotect(const std::string& blob) {
    DATA_BLOB in{static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof kEntropy - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string r(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}
#endif

KeyProtector currentProtector() {
    std::lock_guard<std::mutex> lk(protectorMutex());
    if (protectorRef().protect && protectorRef().unprotect) return protectorRef();
    KeyProtector p;
#if defined(_WIN32)
    p.protect = dpapiProtect;
    p.unprotect = dpapiUnprotect;
#endif
    return p;
}

} // namespace

void setKeyProtector(KeyProtector p) {
    std::lock_guard<std::mutex> lk(protectorMutex());
    protectorRef() = std::move(p);
}

std::string keyFile() { return detail::join(root(), "cle.bin"); }

bool keyProtectionAvailable() {
    const KeyProtector p = currentProtector();
    return p.protect && p.unprotect;
}

std::string keyProtectionText() {
#if defined(_WIN32)
    return "prot\xC3\xA9g\xC3\xA9" "e par Windows (DPAPI, li\xC3\xA9" "e \xC3\xA0 ta session)";
#else
    return keyProtectionAvailable() ? std::string("prot\xC3\xA9g\xC3\xA9" "e (essai)")
                                    : std::string("gard\xC3\xA9" "e pour cette s\xC3\xA9" "ance seulement (Linux : jamais \xC3\xA9" "crite)");
#endif
}

bool storeKey(const std::string& key, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const KeyProtector p = currentProtector();
    if (!p.protect) return fail("Sous Linux, la cl\xC3\xA9 est gard\xC3\xA9" "e pour cette s\xC3\xA9" "ance seulement : elle n'est jamais \xC3\xA9" "crite.");
    if (root().empty()) return fail("Pas de dossier pour l'Assistant : la cl\xC3\xA9 reste en m\xC3\xA9moire.");
    if (key.empty()) return fail("Pas de cl\xC3\xA9 \xC3\xA0 m\xC3\xA9moriser.");
    const auto blob = p.protect(key);
    if (!blob || blob->empty()) return fail("Windows n'a pas pu prot\xC3\xA9ger la cl\xC3\xA9 (DPAPI) : elle reste en m\xC3\xA9moire pour cette s\xC3\xA9" "ance.");
    std::error_code ec;
    fs::create_directories(P(root()), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    if (!core::writeFileAtomic(P(keyFile()), std::string(kMagic) + *blob, how))
        return fail("\xC3\x89" "criture impossible : " + keyFile());
    return true;
}

std::optional<std::string> loadStoredKey(std::string* why) {
    if (root().empty()) return std::nullopt;
    std::string text;
    std::error_code ec;
    if (!fs::exists(P(keyFile()), ec) || !core::readFileAll(P(keyFile()), text)) return std::nullopt;
    const KeyProtector p = currentProtector();
    if (text.rfind(kMagic, 0) != 0 || !p.unprotect) {
        if (why) *why = "La cl\xC3\xA9 m\xC3\xA9moris\xC3\xA9" "e est illisible sur ce poste : entre-la de nouveau.";
        return std::nullopt;
    }
    auto key = p.unprotect(text.substr(kMagic.size()));
    if (!key || key->empty()) {
        if (why) *why = "La cl\xC3\xA9 m\xC3\xA9moris\xC3\xA9" "e ne se relit pas (une autre session Windows, un autre PC) : entre-la de nouveau.";
        return std::nullopt;
    }
    return key;
}

bool hasStoredKey() {
    std::error_code ec;
    return !root().empty() && fs::exists(P(keyFile()), ec);
}

bool forgetStoredKey() {
    if (root().empty()) return true;
    std::error_code ec;
    fs::remove(P(keyFile()), ec);
    return !fs::exists(P(keyFile()), ec);
}

} // namespace app::ai
