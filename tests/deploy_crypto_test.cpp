// =============================================================================
//  tests/deploy_crypto_test.cpp - 1.28.0 : le socle du deploiement reseau
// -----------------------------------------------------------------------------
//  libsodium et mbedTLS, integres (third_party/) : SHA-256 sur les vecteurs de la
//  norme (FIPS 180-2), Ed25519 sur le vecteur 1 de la RFC 8032, une signature
//  fausse refusee, le code a 6 chiffres, l'hexadecimal, la protection locale
//  (aller-retour) ; mbedTLS : sa version et TLS 1.3 dans sa configuration.
// =============================================================================
#include "../src/deploy/Crypto.hpp"

#include <mbedtls/build_info.h>
#include <mbedtls/version.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

} // namespace

int main() {
    namespace c = deploy::crypto;
    check(c::init(), "libsodium pret");
    // FIPS 180-2 : "abc" et la chaine vide.
    check(c::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256('abc')");
    check(c::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256('')");
    // RFC 8032, 7.1, TEST 1 : la cle secrete (graine), la cle publique, la signature du message vide.
    {
        c::Bytes seed, pub, sig;
        check(c::unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32), "graine RFC 8032");
        check(c::unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pub, 32), "cle publique RFC 8032");
        check(c::unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
                       sig, 64),
              "signature RFC 8032");
        // la cle secrete Ed25519 de libsodium : la graine puis la cle publique
        c::SecretKey sk{};
        std::memcpy(sk.data(), seed.data(), 32);
        std::memcpy(sk.data() + 32, pub.data(), 32);
        const auto s = c::sign(sk, "");
        check(std::memcmp(s.data(), sig.data(), 64) == 0, "Ed25519 : la signature de la RFC 8032");
        c::PublicKey pk{};
        std::memcpy(pk.data(), pub.data(), 32);
        check(c::verify(pk, "", s), "Ed25519 : la signature se verifie");
        check(!c::verify(pk, "x", s), "Ed25519 : un autre message est refuse");
    }
    {
        const auto k = c::newSignKeys();
        auto s = c::sign(k.secret, "manifeste");
        check(c::verify(k.pub, "manifeste", s), "une paire neuve : signe et verifie");
        s[10] ^= 1;
        check(!c::verify(k.pub, "manifeste", s), "un bit change : refuse");
        const auto other = c::newSignKeys();
        check(!c::verify(other.pub, "manifeste", c::sign(k.secret, "manifeste")), "une autre cle publique : refuse");
    }
    {
        const auto code = c::randomDigits(6);
        bool digits = code.size() == 6;
        for (char ch : code) digits = digits && ch >= '0' && ch <= '9';
        check(digits, "le code d'appairage : 6 chiffres (" + code + ")");
        c::Bytes b;
        check(!c::unhex("zz", b) && !c::unhex("abc", b) && c::unhex("00ff", b, 2) && b.size() == 2 && b[1] == 0xff, "l'hexadecimal");
        c::Digest d{};
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = static_cast<std::uint8_t>(i);
        check(c::fingerprintText(d, 2) == "00:01:\xE2\x80\xA6:1E:1F", "l'empreinte lisible : " + c::fingerprintText(d, 2));
    }
    {
        const c::Bytes secret = {1, 2, 3, 4, 5};
        c::Bytes sealed, back;
        std::string why;
        check(c::protect(secret, sealed, false, &why) && c::unprotect(sealed, back, &why) && back == secret, "proteger puis relire [" + why + "]");
    }
    check(std::string(MBEDTLS_VERSION_STRING) == "3.6.2", std::string("mbedTLS 3.6.2 (") + MBEDTLS_VERSION_STRING + ")");
#if defined(MBEDTLS_SSL_PROTO_TLS1_3) && defined(MBEDTLS_SSL_SRV_C) && defined(MBEDTLS_SSL_CLI_C) && defined(MBEDTLS_X509_CRT_WRITE_C)
    check(true, "mbedTLS : TLS 1.3, serveur, client, certificats");
#else
    check(false, "mbedTLS : TLS 1.3, serveur, client, certificats");
#endif
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
