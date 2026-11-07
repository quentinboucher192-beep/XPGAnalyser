// =============================================================================
//  tests/key_backup_test.cpp - 1.32.0 : la sauvegarde de la cle de l'editeur,
//  chiffree par une phrase de passe (license_admin/KeyBackup)
// -----------------------------------------------------------------------------
//  Une cle d'ESSAI creee dans un dossier temporaire ; la phrase est celle de
//  l'essai (jamais une vraie). Aller-retour (la meme cle, qui signe pareil),
//  phrase fausse, en-tete ou chiffre modifies, parametres affaiblis ou
//  demesures refuses, une cle deja la jamais remplacee, une phrase trop simple.
// =============================================================================
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/KeyBackup.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace adm = license_admin;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace

int main() {
    if (!deploy::crypto::init()) return 1;
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path base = fs::temp_directory_path() / ("xpg-key-backup-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(base, ec);
    std::string why;
    const std::string phrase = "phrase d'essai : sept chevaux bleus";   // l'essai seulement
    adm::KeyBackupOptions fast;                                         // les essais : le minimum accepte
    fast.opsLimit = 2;
    fast.memLimit = 64u * 1024u * 1024u;

    auto key = adm::createPublisherKey(U(base / "a"), "essai sauvegarde", true, &why);
    check(key != nullptr, "la cle d'essai (" + why + ")");
    if (!key) return 1;

    std::printf("-- la phrase\n");
    check(!adm::acceptablePassphrase("court", &why) && !adm::acceptablePassphrase("123456789012345", &why)
              && !adm::acceptablePassphrase("aaaaaaaaaaaaaaaa", &why) && adm::acceptablePassphrase(phrase, &why),
          "trop courte, que des chiffres, un seul caractere : refusees ; une phrase : acceptee");
    std::string out;
    check(!adm::writeKeyBackup(*key, "aaaaaaaaaaaaaaaaaa", out, &why, fast), "une sauvegarde avec une phrase trop simple : refusee");

    std::printf("-- aller-retour\n");
    std::string backup;
    check(adm::writeKeyBackup(*key, phrase, backup, &why, fast), "la sauvegarde (" + why + ")");
    check(backup.rfind("XPG-SAUVEGARDE-CLE 1\ncle=" + key->keyId() + "\nargon2id ops=2 mem=67108864 sel=", 0) == 0, "l'en-tete : la marque, l'identifiant, Argon2id");
    check(!has(backup, key->publicKeyHex()) && !has(backup, phrase) && !has(backup, "essai sauvegarde"), "ni la cle publique, ni la phrase, ni le nom en clair");
    std::string id, text, name;
    check(adm::describeKeyBackup(backup, id, text, &why) && id == key->keyId() && has(text, "Argon2id"), "l'en-tete se lit sans la phrase : " + text);
    check(adm::checkKeyBackup(backup, phrase, id, name, &why) && id == key->keyId() && name == key->name(), "la phrase ouvre la sauvegarde (" + why + ")");
    check(!adm::checkKeyBackup(backup, phrase + "x", id, name, &why) && has(why, "phrase"), "une phrase fausse : refusee (" + why + ")");
    auto back = adm::restoreKeyBackup(U(base / "b"), backup, phrase, &why);
    check(back && back->keyId() == key->keyId() && back->publicKey() == key->publicKey() && back->test() && back->name() == key->name(),
          "restauree ailleurs : la meme cle (" + why + ")");
    if (back) {
        const std::string msg = "message a signer";
        check(back->sign(msg) == key->sign(msg), "elle signe exactement comme l'originale");
    }
    check(adm::restoreKeyBackup(U(base / "b"), backup, phrase, &why) == nullptr && has(why, "remplac"), "une cle deja la : jamais remplacee (" + why + ")");
    check(adm::restoreKeyBackup(U(base / "c"), backup, "une autre phrase longue", &why) == nullptr && !fs::exists(base / "c" / adm::kPublisherKeyFile),
          "une phrase fausse : rien n'est ecrit");

    std::printf("-- ce qui est modifie\n");
    {
        std::string otherId = backup;
        const auto at = otherId.find("cle=xpg-") + 8;
        otherId[at] = otherId[at] == 'a' ? 'b' : 'a';
        check(!adm::checkKeyBackup(otherId, phrase, id, name, &why), "l'identifiant change dans l'en-tete : refuse");
        std::string weaker = backup;
        const auto ops = weaker.find("ops=2");
        weaker.replace(ops, 5, "ops=1");
        check(!adm::checkKeyBackup(weaker, phrase, id, name, &why) && has(why, "param"), "des parametres affaiblis (1 passe) : refuses (" + why + ")");
        std::string huge = backup;
        const auto mem = huge.find("mem=67108864");
        huge.replace(mem, 12, "mem=9999999999");
        check(!adm::checkKeyBackup(huge, phrase, id, name, &why) && has(why, "param"), "une memoire demesuree : refusee");
        std::string damaged = backup;
        const auto body = damaged.find('\n', damaged.find("sel=")) + 10;
        damaged[body] = damaged[body] == 'A' ? 'B' : 'A';
        check(!adm::checkKeyBackup(damaged, phrase, id, name, &why), "le chiffre modifie : refuse");
        check(!adm::checkKeyBackup("pas une sauvegarde\n", phrase, id, name, &why) && has(why, "XPG-SAUVEGARDE-CLE"), "un autre fichier : refuse");
    }
    std::string backup2;
    check(adm::writeKeyBackup(*key, phrase, backup2, &why, fast) && backup2 != backup, "deux sauvegardes de la meme cle ne se ressemblent pas (sel et nonce)");

    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
