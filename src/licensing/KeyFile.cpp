// =============================================================================
//  licensing/KeyFile.cpp - 1.30.0 : une paire Ed25519 gardee sur ce PC (voir KeyFile.hpp)
// =============================================================================
#include "KeyFile.hpp"

#include "SignedFile.hpp"

#include <algorithm>
#include <filesystem>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace licensing {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string protectionKey(bool machine) {
    if (!crypto::protectedHere()) return "fichier";
    return machine ? "dpapi-machine" : "dpapi-compte";
}

std::string cleanName(std::string_view raw) {
    std::string s;
    for (char c : raw) s += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? ' ' : c;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() > 100) {
        std::size_t cut = 100;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
    }
    return s;
}

void wipeString(std::string& s) noexcept {
    if (!s.empty()) crypto::wipe(s.data(), s.size());
    s.clear();
}

} // namespace

const KeyFileSpec& deviceKeySpec() {
    static const KeyFileSpec spec{
        "xpg-identite-pc/1", /*machine=*/true,
        "XPG - l'identit\xC3\xA9 de ce PC pour les licences (une paire de cl\xC3\xA9s Ed25519 cr\xC3\xA9\xC3\xA9" "e sur ce PC).\n"
        "La licence est li\xC3\xA9" "e \xC3\xA0 sa cl\xC3\xA9 publique. Ne copie pas ce fichier : sa partie secr\xC3\xA8te ne se relit que sur ce PC.",
        "la cl\xC3\xA9 d'identit\xC3\xA9 de ce PC"};
    return spec;
}

std::unique_ptr<SigningKey> SigningKey::loadOrCreate(const std::string& path, const KeyFileSpec& spec, const std::string& name,
                                                     std::string* why) {
    std::error_code ec;
    if (std::filesystem::exists(P(path), ec)) return load(path, spec, why);
    return create(path, spec, name, false, why);
}

std::unique_ptr<SigningKey> SigningKey::create(const std::string& path, const KeyFileSpec& spec, const std::string& name, bool test,
                                               std::string* why) {
    if (!crypto::init()) {
        if (why) *why = "le hasard du syst\xC3\xA8me est indisponible : " + spec.what + " ne peut pas \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e";
        return nullptr;
    }
    std::error_code ec;
    if (std::filesystem::exists(P(path), ec)) {
        if (why) *why = spec.what + " existe d\xC3\xA9j\xC3\xA0 (" + path + ") : elle n'est pas remplac\xC3\xA9" "e";
        return nullptr;
    }
    std::unique_ptr<SigningKey> key(new SigningKey());
    key->path_ = path;
    key->name_ = cleanName(name);
    key->test_ = test;
    auto pair = crypto::newSignKeys();
    key->pub_ = pair.pub;
    key->secret_ = pair.secret;
    crypto::wipe(pair.secret);
    if (!key->save(spec, why)) return nullptr;
    // Relue du disque : c'est elle qui fait foi (et la relecture est verifiee).
    auto back = load(path, spec, why);
    if (!back) return nullptr;
    back->created_ = back->pub_ == key->pub_;
    return back;
}

std::unique_ptr<SigningKey> SigningKey::load(const std::string& path, const KeyFileSpec& spec, std::string* why) {
    const auto fail = [&](const std::string& text) -> std::unique_ptr<SigningKey> {
        if (why) *why = spec.what + " (" + path + ") ne se relit pas : " + text + ". Elle n'est pas remplac\xC3\xA9" "e.";
        return nullptr;
    };
    if (!crypto::init()) return fail("libsodium ne d\xC3\xA9marre pas");
#ifndef _WIN32
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0 && (st.st_mode & 077) != 0) ::chmod(path.c_str(), 0600);
#endif
    crypto::Bytes raw;
    std::string readWhy;
    if (!crypto::readFile(path, raw, &readWhy, 64u * 1024u)) return fail(readWhy);
    const std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string format, name, test, pubHex, protection, secretHex;
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
        else if (k == "essai") test = v;
        else if (k == "publique") pubHex = v;
        else if (k == "protection") protection = v;
        else if (k == "secrete") secretHex = v;
    }
    crypto::wipe(raw.data(), raw.size());

    std::unique_ptr<SigningKey> key(new SigningKey());
    key->path_ = path;
    key->name_ = cleanName(name);
    key->test_ = test == "oui";
    if (format != spec.format) {
        wipeString(secretHex);
        return fail(format.empty() ? std::string("ce n'est pas un fichier de cl\xC3\xA9") : "ce n'est pas le bon genre de cl\xC3\xA9 (format " + format + ")");
    }
    crypto::Bytes pub;
    if (!crypto::unhex(pubHex, pub, 32)) {
        wipeString(secretHex);
        return fail("la cl\xC3\xA9 publique est illisible");
    }
    if (protection != protectionKey(spec.machine)) {
        wipeString(secretHex);
        return fail("elle a \xC3\xA9t\xC3\xA9 prot\xC3\xA9g\xC3\xA9" "e autrement (\xC2\xAB " + protection + " \xC2\xBB) : elle ne se relit que l\xC3\xA0 o\xC3\xB9 elle a \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e");
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
    static constexpr std::string_view kProbe = "xpg-licences-controle-de-cle";
    if (!samePub || !crypto::verify(key->pub_, kProbe, key->sign(kProbe)))
        return fail("la partie secr\xC3\xA8te ne correspond pas \xC3\xA0 la cl\xC3\xA9 publique");
    return key;
}

bool SigningKey::save(const KeyFileSpec& spec, std::string* why) const {
    crypto::Bytes plain(secret_.begin(), secret_.end()), sealed;
    std::string protectWhy;
    const bool ok = crypto::protect(plain, sealed, spec.machine, &protectWhy);
    crypto::wipe(plain.data(), plain.size());
    if (!ok) {
        if (why) *why = spec.what + " ne peut pas \xC3\xAAtre prot\xC3\xA9g\xC3\xA9" "e : " + protectWhy;
        return false;
    }
    std::string text;
    std::size_t pos = 0;
    while (pos < spec.comment.size()) {
        std::size_t eol = spec.comment.find('\n', pos);
        if (eol == std::string::npos) eol = spec.comment.size();
        text += "# " + spec.comment.substr(pos, eol - pos) + "\n";
        pos = eol + 1;
    }
    text += crypto::protectedHere() ? std::string("# Partie secr\xC3\xA8te prot\xC3\xA9g\xC3\xA9" "e par DPAPI (") + (spec.machine ? "ce PC" : "ce compte Windows") + ").\n"
                                    : std::string("# Partie secr\xC3\xA8te en clair : fichier lisible par son seul propri\xC3\xA9taire.\n");
    text += "format=" + spec.format + "\n";
    text += "nom=" + name_ + "\n";
    text += std::string("essai=") + (test_ ? "oui" : "non") + "\n";
    text += "publique=" + crypto::hex(pub_) + "\n";
    text += "protection=" + protectionKey(spec.machine) + "\n";
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
        if (why) *why = spec.what + " ne peut pas \xC3\xAAtre \xC3\xA9" "crite : " + writeWhy;
        return false;
    }
    return true;
}

SigningKey::~SigningKey() { crypto::wipe(secret_); }

std::string SigningKey::keyId() const { return keyIdOf(pub_); }

std::string SigningKey::fingerprint(std::size_t groups) const { return publicKeyFingerprint(pub_, groups); }

crypto::Signature SigningKey::sign(std::string_view message) const noexcept { return crypto::sign(secret_, message); }

std::string publicKeyFingerprint(const crypto::PublicKey& key, std::size_t groups) {
    return crypto::fingerprintText(crypto::sha256(key.data(), key.size()), groups);
}

} // namespace licensing
