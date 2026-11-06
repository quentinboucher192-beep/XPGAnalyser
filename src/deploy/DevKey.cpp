// =============================================================================
//  deploy/DevKey.cpp - 1.28.0 : la cle d'editeur (voir DevKey.hpp)
// =============================================================================
#include "DevKey.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace deploy {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

constexpr std::size_t kMaxName = 64;   // octets UTF-8

// Le nom d'un editeur : une ligne, sans caractere de controle, 64 octets au plus (sans couper un caractere).
std::string cleanName(std::string_view raw) {
    std::string s;
    for (char c : raw) s += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? ' ' : c;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() > kMaxName) {
        std::size_t cut = kMaxName;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        s.resize(cut);
    }
    return s;
}

// La protection ecrite dans le fichier : elle doit etre celle de ce systeme pour se relire.
const char* protectionKey() { return crypto::protectedHere() ? "dpapi" : "fichier"; }

void wipeString(std::string& s) noexcept {
    if (!s.empty()) crypto::wipe(s.data(), s.size());
    s.clear();
}

} // namespace

std::string DevKey::filePath(const std::string& folder) {
    const auto u8 = (P(folder) / kFileName).u8string();
    return std::string(u8.begin(), u8.end());
}

std::string DevKey::computerName() {
#ifdef _WIN32
    wchar_t buf[256];
    DWORD n = 256;
    if (!GetComputerNameExW(ComputerNameDnsHostname, buf, &n) || n == 0) {
        n = 256;
        if (!GetComputerNameW(buf, &n)) n = 0;
    }
    std::string out;
    if (n > 0) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            out.resize(static_cast<std::size_t>(len));
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), len, nullptr, nullptr);
        }
    }
#else
    char buf[256] = {};
    std::string out;
    if (::gethostname(buf, sizeof buf - 1) == 0) out = buf;
#endif
    out = cleanName(out);
    return out.empty() ? std::string("PC") : out;
}

std::unique_ptr<DevKey> DevKey::loadOrCreate(const std::string& folder, std::string* why, const std::string& name) {
    if (!crypto::init()) {
        if (why) *why = "le hasard du syst\xC3\xA8me est indisponible : la cl\xC3\xA9 d'\xC3\xA9" "diteur ne peut pas \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e";
        return nullptr;
    }
    if (folder.empty()) {
        if (why) *why = "aucun dossier pour la cl\xC3\xA9 d'\xC3\xA9" "diteur";
        return nullptr;
    }
    const std::string path = filePath(folder);
    std::error_code ec;
    if (std::filesystem::exists(P(path), ec)) return load(path, why);

    // Premiere utilisation : une paire neuve.
    std::unique_ptr<DevKey> key(new DevKey());
    key->path_ = path;
    key->name_ = cleanName(name);
    if (key->name_.empty()) key->name_ = computerName();
    auto pair = crypto::newSignKeys();
    key->pub_ = pair.pub;
    key->secret_ = pair.secret;
    crypto::wipe(pair.secret);
    if (!key->save(why)) return nullptr;
    // Relue du disque : c'est elle qui fait foi (et la relecture est verifiee).
    auto back = load(path, why);
    if (!back) return nullptr;
    back->created_ = back->pub_ == key->pub_;
    return back;
}

std::unique_ptr<DevKey> DevKey::load(const std::string& path, std::string* why) {
    const auto fail = [&](const std::string& text) -> std::unique_ptr<DevKey> {
        if (why)
            *why = "la cl\xC3\xA9 d'\xC3\xA9" "diteur (" + path + ") ne se relit pas : " + text
                   + ". Elle n'est pas remplac\xC3\xA9" "e : supprime ce fichier pour en cr\xC3\xA9" "er une neuve "
                     "(les PC appair\xC3\xA9s devront l'\xC3\xAAtre de nouveau).";
        return nullptr;
    };
#ifndef _WIN32
    // Lisible par son seul proprietaire : remis en 0600 si quelqu'un l'a elargi.
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0 && (st.st_mode & 077) != 0) ::chmod(path.c_str(), 0600);
#endif
    crypto::Bytes raw;
    std::string readWhy;
    if (!crypto::readFile(path, raw, &readWhy, 64u * 1024u)) return fail(readWhy);
    const std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());

    std::string format, name, pubHex, protection, secretHex;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const auto k = line.substr(0, eq);
        const auto v = line.substr(eq + 1);
        if (k == "format") format = v;
        else if (k == "nom") name = v;
        else if (k == "publique") pubHex = v;
        else if (k == "protection") protection = v;
        else if (k == "secrete") secretHex = v;
    }
    crypto::wipe(raw.data(), raw.size());

    std::unique_ptr<DevKey> key(new DevKey());
    key->path_ = path;
    key->name_ = cleanName(name);
    if (key->name_.empty()) key->name_ = computerName();
    if (format != "1") {
        wipeString(secretHex);
        return fail(format.empty() ? std::string("ce n'est pas un fichier de cl\xC3\xA9 d'\xC3\xA9" "diteur")
                                   : "format " + format + " inconnu (une version plus r\xC3\xA9" "cente l'a \xC3\xA9" "crit ?)");
    }
    crypto::Bytes pub;
    if (!crypto::unhex(pubHex, pub, 32)) {
        wipeString(secretHex);
        return fail("la cl\xC3\xA9 publique est illisible");
    }
    if (protection != protectionKey()) {
        wipeString(secretHex);
        return fail(protection == "dpapi" ? "elle a \xC3\xA9t\xC3\xA9 prot\xC3\xA9g\xC3\xA9" "e par DPAPI sous Windows : elle ne se relit que sur ce PC, avec ce compte"
                                          : "elle n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite sur ce syst\xC3\xA8me (protection \xC2\xAB " + protection + " \xC2\xBB)");
    }
    crypto::Bytes sealed, plain;
    const bool hexOk = crypto::unhex(secretHex, sealed);
    wipeString(secretHex);
    if (!hexOk) return fail("la partie secr\xC3\xA8te est illisible");
    std::string unprotectWhy;
    const bool opened = crypto::unprotect(sealed, plain, &unprotectWhy);
    crypto::wipe(sealed.data(), sealed.size());
    if (!opened) return fail(unprotectWhy);
    if (plain.size() != key->secret_.size()) {
        crypto::wipe(plain.data(), plain.size());
        return fail("la partie secr\xC3\xA8te n'a pas la bonne taille");
    }
    std::copy(plain.begin(), plain.end(), key->secret_.begin());
    crypto::wipe(plain.data(), plain.size());
    std::copy(pub.begin(), pub.end(), key->pub_.begin());

    // La paire se tient : la cle secrete porte la publique (libsodium : graine puis publique), et signe pour elle.
    const bool samePub = std::equal(key->pub_.begin(), key->pub_.end(), key->secret_.begin() + 32);
    static constexpr std::string_view kProbe = "xpg-cle-editeur-controle";
    if (!samePub || !crypto::verify(key->pub_, kProbe, key->sign(kProbe)))
        return fail("la partie secr\xC3\xA8te ne correspond pas \xC3\xA0 la cl\xC3\xA9 publique");
    return key;
}

bool DevKey::save(std::string* why) const {
    crypto::Bytes plain(secret_.begin(), secret_.end()), sealed;
    std::string protectWhy;
    const bool ok = crypto::protect(plain, sealed, /*machine=*/false, &protectWhy);
    crypto::wipe(plain.data(), plain.size());
    if (!ok) {
        if (why) *why = "la cl\xC3\xA9 d'\xC3\xA9" "diteur ne peut pas \xC3\xAAtre prot\xC3\xA9g\xC3\xA9" "e : " + protectWhy;
        return false;
    }
    std::string text = "# XPG - cl\xC3\xA9 d'\xC3\xA9" "diteur (Ed25519) de ce PC de d\xC3\xA9veloppement. Elle signe les paquets de d\xC3\xA9ploiement.\n"
                       "# Ne la copie pas et ne la modifie pas : la partie secr\xC3\xA8te ne se relit que sur ce PC"
                       + std::string(crypto::protectedHere() ? ", avec ce compte Windows (DPAPI).\n" : " (fichier lisible par son seul propri\xC3\xA9taire).\n");
    text += "format=1\n";
    text += "nom=" + name_ + "\n";
    text += "publique=" + crypto::hex(pub_) + "\n";
    text += std::string("protection=") + protectionKey() + "\n";
    std::string secretHex = crypto::hex(sealed.data(), sealed.size());
    crypto::wipe(sealed.data(), sealed.size());
    text += "secrete=" + secretHex + "\n";
    wipeString(secretHex);
    crypto::Bytes bytes(text.begin(), text.end());
    wipeString(text);
    std::string writeWhy;
    const bool written = crypto::writePrivateFile(path_, bytes, &writeWhy);
    crypto::wipe(bytes.data(), bytes.size());
    if (!written) {
        if (why) *why = "la cl\xC3\xA9 d'\xC3\xA9" "diteur ne peut pas \xC3\xAAtre \xC3\xA9" "crite : " + writeWhy;
        return false;
    }
    return true;
}

DevKey::~DevKey() { crypto::wipe(secret_); }

std::string DevKey::fingerprint(std::size_t groups) const { return keyFingerprint(pub_, groups); }

bool DevKey::rename(const std::string& name, std::string* why) {
    const std::string clean = cleanName(name);
    if (clean.empty()) {
        if (why) *why = "le nom d'\xC3\xA9" "diteur est vide";
        return false;
    }
    const std::string before = name_;
    name_ = clean;
    if (!save(why)) {
        name_ = before;
        return false;
    }
    return true;
}

crypto::Signature DevKey::sign(std::string_view message) const noexcept { return crypto::sign(secret_, message); }

std::string keyFingerprint(const crypto::PublicKey& key, std::size_t groups) {
    return crypto::fingerprintText(crypto::sha256(key.data(), key.size()), groups);
}

} // namespace deploy
