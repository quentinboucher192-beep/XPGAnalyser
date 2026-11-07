// =============================================================================
//  licensing/SignedFile.cpp - 1.30.0 : le fichier signe (voir SignedFile.hpp)
// =============================================================================
#include "SignedFile.hpp"

#include <algorithm>

namespace licensing {

namespace {

constexpr std::string_view kMagic = "XPG-LICENSING 1 ";
constexpr std::string_view kSignature = "SIGNATURE ed25519 ";
constexpr std::string_view kContext = "XPG-LICENSING-1\n";

bool kindFromKey(std::string_view key, DocKind& out) noexcept {
    for (DocKind k : {DocKind::License, DocKind::Registry, DocKind::Revocations, DocKind::Request, DocKind::Gateway, DocKind::DeviceStatus,
                       DocKind::Cancel, DocKind::ServiceCert, DocKind::Activation, DocKind::Lease, DocKind::Time, DocKind::Trial})
        if (key == docKindKey(k)) {
            out = k;
            return true;
        }
    return false;
}

bool isKeyId(std::string_view s) noexcept {
    if (s.size() != 20 || s.substr(0, 4) != "xpg-") return false;
    return std::all_of(s.begin() + 4, s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Une ligne qui finit par '\n' a partir de `pos` (sans le '\n'). Faux : pas de fin de ligne (tronque).
bool takeLine(std::string_view file, std::size_t& pos, std::string_view& line, std::size_t maxLen) noexcept {
    const std::size_t eol = file.find('\n', pos);
    if (eol == std::string_view::npos || eol - pos > maxLen) return false;
    line = file.substr(pos, eol - pos);
    pos = eol + 1;
    return true;
}

} // namespace

const char* docKindKey(DocKind k) noexcept {
    switch (k) {
        case DocKind::License: return "licence";
        case DocKind::Registry: return "registre";
        case DocKind::Revocations: return "revocations";
        case DocKind::Request: return "demande";
        case DocKind::Gateway: return "passerelle";
        case DocKind::DeviceStatus: return "poste";
        case DocKind::Cancel: return "annulation";
        case DocKind::ServiceCert: return "service";
        case DocKind::Activation: return "activation";
        case DocKind::Lease: return "jeton";
        case DocKind::Time: return "heure";
        case DocKind::Trial: return "decouverte";
    }
    return "?";
}

std::string docKindLabel(DocKind k) {
    switch (k) {
        case DocKind::License: return "la licence";
        case DocKind::Registry: return "le registre";
        case DocKind::Revocations: return "la liste de r\xC3\xA9vocation";
        case DocKind::Request: return "la demande d'activation";
        case DocKind::Gateway: return "la configuration de la passerelle";
        case DocKind::DeviceStatus: return "l'\xC3\xA9tat du PC";
        case DocKind::Cancel: return "l'annulation de la demande";
        case DocKind::ServiceCert: return "le certificat du service";
        case DocKind::Activation: return "l'activation";
        case DocKind::Lease: return "le jeton de la licence flottante";
        case DocKind::Time: return "l'heure du service";
        case DocKind::Trial: return "la D\xC3\xA9" "couverte";
    }
    return "le document";
}

std::size_t docKindMaxBytes(DocKind k) noexcept {
    switch (k) {
        case DocKind::License: return 256u * 1024u;
        case DocKind::Registry: return 2u * 1024u * 1024u;
        case DocKind::Revocations: return 8u * 1024u * 1024u;
        case DocKind::Request: return 16u * 1024u;
        case DocKind::Gateway: return 16u * 1024u;
        case DocKind::DeviceStatus: return 512u * 1024u;   // la licence scellee dedans
        case DocKind::Cancel: return 4u * 1024u;
        case DocKind::ServiceCert: return 4u * 1024u;
        case DocKind::Activation: return 4u * 1024u;
        case DocKind::Lease: return 2u * 1024u;
        case DocKind::Time: return 1024u;
        case DocKind::Trial: return 2u * 1024u;
    }
    return 0;
}

std::string keyIdOf(const crypto::PublicKey& key) {
    const auto d = crypto::sha256(key.data(), key.size());
    return "xpg-" + crypto::hex(d.data(), 8);
}

std::string signedMessage(DocKind kind, std::string_view document) {
    std::string m(kContext);
    m += docKindKey(kind);
    m += '\n';
    m.append(document.data(), document.size());
    return m;
}

SignedBlock signBlock(DocKind kind, std::string document, const crypto::PublicKey& pub, const Signer& signer) {
    SignedBlock b;
    b.kind = kind;
    b.document = std::move(document);
    b.keyId = keyIdOf(pub);
    b.signature = signer(signedMessage(kind, b.document));
    return b;
}

bool verifyBlock(const SignedBlock& block, const crypto::PublicKey& pub) noexcept {
    try {
        if (block.keyId != keyIdOf(pub)) return false;
        return crypto::verify(pub, signedMessage(block.kind, block.document), block.signature);
    } catch (...) {
        return false;
    }
}

std::string writeBlock(const SignedBlock& b) {
    std::string out(kMagic);
    out += docKindKey(b.kind);
    out += ' ';
    out += std::to_string(b.document.size());
    out += '\n';
    out += b.document;
    out += kSignature;
    out += b.keyId;
    out += ' ';
    out += crypto::hex(b.signature);
    out += '\n';
    return out;
}

bool readBlocks(std::string_view file, std::vector<SignedBlock>& out, std::string* why) {
    const auto fail = [&](const std::string& text) {
        if (why) *why = text;
        return false;
    };
    out.clear();
    if (file.size() > kMaxSignedFileBytes) return fail("le fichier est trop gros pour \xC3\xAAtre une licence");
    std::size_t pos = 0;
    // Un BOM UTF-8 en tete (un fichier passe par un editeur) : ignore ; le reste doit etre intact.
    if (file.substr(0, 3) == "\xEF\xBB\xBF") pos = 3;
    while (true) {
        // Les lignes vides entre les blocs, et a la fin, sont permises.
        while (pos < file.size() && (file[pos] == '\n' || file[pos] == '\r' || file[pos] == ' ' || file[pos] == '\t')) ++pos;
        if (pos >= file.size()) break;
        if (out.size() >= kMaxBlocksPerFile) return fail("le fichier contient trop de documents");
        std::string_view line;
        if (file.substr(pos, kMagic.size()) != kMagic) {
            return fail(out.empty() ? "ce n'est pas un fichier de licence XPG (en-t\xC3\xAAte absent)"
                                    : "du texte inattendu suit le document n\xC2\xB0 " + std::to_string(out.size()));
        }
        if (!takeLine(file, pos, line, 128)) return fail("le fichier est tronqu\xC3\xA9 (en-t\xC3\xAAte incomplet)");
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line.remove_prefix(kMagic.size());
        const std::size_t sp = line.find(' ');
        if (sp == std::string_view::npos) return fail("l'en-t\xC3\xAAte du document n\xC2\xB0 " + std::to_string(out.size() + 1) + " est illisible");
        SignedBlock b;
        if (!kindFromKey(line.substr(0, sp), b.kind))
            return fail("genre de document inconnu : \xC2\xAB " + std::string(line.substr(0, sp)) + " \xC2\xBB (une version plus r\xC3\xA9" "cente l'a \xC3\xA9" "crit ?)");
        const std::string_view sizeText = line.substr(sp + 1);
        if (sizeText.empty() || sizeText.size() > 9 || !std::all_of(sizeText.begin(), sizeText.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return fail("la taille du document n\xC2\xB0 " + std::to_string(out.size() + 1) + " est illisible");
        const std::size_t size = static_cast<std::size_t>(std::stoul(std::string(sizeText)));
        if (size > docKindMaxBytes(b.kind)) return fail("le document \xC2\xAB " + std::string(docKindKey(b.kind)) + " \xC2\xBB est trop gros ou ab\xC3\xAEm\xC3\xA9 (" + std::to_string(size) + " octets annonc\xC3\xA9s)");
        if (file.size() - pos < size) return fail("le fichier est tronqu\xC3\xA9 : le document \xC2\xAB " + std::string(docKindKey(b.kind)) + " \xC2\xBB est incomplet");
        b.document.assign(file.data() + pos, size);
        pos += size;
        if (file.substr(pos, kSignature.size()) != kSignature) {
            return fail(pos >= file.size() || file.size() - pos < kSignature.size()
                            ? "le fichier est tronqu\xC3\xA9 : la signature de " + docKindLabel(b.kind) + " manque"
                            : "le document n\xC2\xB0 " + std::to_string(out.size() + 1) + " a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 (sa taille ne correspond plus)");
        }
        if (!takeLine(file, pos, line, 256)) return fail("le fichier est tronqu\xC3\xA9 : la signature de " + docKindLabel(b.kind) + " est incompl\xC3\xA8te");
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line.remove_prefix(kSignature.size());
        const std::size_t sp2 = line.find(' ');
        if (sp2 == std::string_view::npos || !isKeyId(line.substr(0, sp2)))
            return fail("la signature de " + docKindLabel(b.kind) + " est illisible");
        b.keyId = std::string(line.substr(0, sp2));
        crypto::Bytes sig;
        if (!crypto::unhex(line.substr(sp2 + 1), sig, b.signature.size()))
            return fail("la signature de " + docKindLabel(b.kind) + " est illisible");
        std::copy(sig.begin(), sig.end(), b.signature.begin());
        out.push_back(std::move(b));
    }
    if (out.empty()) return fail("le fichier est vide");
    return true;
}

} // namespace licensing
