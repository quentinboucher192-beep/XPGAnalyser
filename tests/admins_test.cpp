// =============================================================================
//  tests/admins_test.cpp - 1.44.0 : plusieurs admins (docs/ADMINS.md)
// -----------------------------------------------------------------------------
//  Les droits, l'empreinte d'une cle, la liste des admins (ecrite, relue,
//  controlee) ; un document signe par un admin (son droit, retire, garde) ;
//  le PC d'un client (LicenseStore) : une licence signee par Lea est refusee
//  tant qu'il n'a pas la liste, acceptee ensuite ; la liste plus ancienne ne
//  revient pas ; un fichier a deux blocs (la liste, puis la licence) ; un admin
//  retire : ce qui est garde vaut, le reste non ; un droit retire ne casse pas
//  ce qui est garde. Le journal (entree signee, ligne chainee), les fichiers,
//  les versions, les messages, le jeton d'admin. Des noms d'essai, une cle
//  d'ESSAI (admin_test_fixture.hpp).
// =============================================================================
#include "admin_test_fixture.hpp"

#include "../src/core/Json.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/SignedFile.hpp"

using namespace admintest;
namespace adm = licensing::admins;

namespace {

std::unique_ptr<lic::SigningKey> makeKey(const fs::path& dir, const std::string& name) {
    fs::create_directories(dir);
    std::string why;
    auto k = lic::SigningKey::create(U(dir / "cle.cle"), lic::deviceKeySpec(), name, false, &why);
    if (!k) std::printf("  cle impossible : %s\n", why.c_str());
    return k;
}

lic::SignedBlock signWith(const lic::SigningKey& k, lic::DocKind kind, const std::string& doc) {
    return lic::signBlock(kind, doc, k.publicKey(), [&](std::string_view m) { return k.sign(m); });
}

adm::Admin adminOf(const lic::SigningKey& k, const std::string& name, std::vector<std::string> rights) {
    adm::Admin a;
    a.key = k.publicKeyHex();
    a.id = lic::keyIdOf(k.publicKey());
    a.name = name;
    a.pc = "PC-ESSAI-" + name;
    a.machine = std::string(32, 'a');
    a.since = g_now;
    a.profile = "Admin LV1";
    a.rights = std::move(rights);
    return a;
}

std::string listText(const lic::SigningKey& editor, int number, const std::vector<adm::Admin>& admins) {
    adm::AdminList l;
    l.number = number;
    l.issued = g_now;
    l.admins = admins;
    return lic::writeBlock(signWith(editor, lic::DocKind::Admins, adm::writeAdminList(l)));
}

// Un bloc d'un genre d'un fichier exporte par l'Admin (la licence, le registre).
bool blockOf(const fs::path& file, lic::DocKind kind, lic::SignedBlock& out) {
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(readFile(file), blocks, nullptr)) return false;
    for (auto& b : blocks)
        if (b.kind == kind) {
            out = b;
            return true;
        }
    return false;
}
bool licenceBlockOf(const fs::path& file, lic::SignedBlock& out) { return blockOf(file, lic::DocKind::License, out); }

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    if (!deploy::crypto::init()) return 1;

    std::printf("-- les droits\n");
    {
        check(adm::rights().size() == 13 && adm::allRights().size() == 13, "13 droits");
        check(adm::isRight("licence_creer") && adm::isRight("courriels") && !adm::isRight("admin_ajouter"), "les identifiants");
        check(adm::rightsForKind(lic::DocKind::License).size() == 3 && adm::rightsForKind(lic::DocKind::Revocations) == std::vector<std::string>{"licence_revoquer"},
              "les droits d'un genre");
        check(adm::rightsForKind(lic::DocKind::Admins).empty() && adm::rightsForKind(lic::DocKind::Version).empty(), "la liste, une version : l'editeur seul");
        check(adm::normalizeRights({"demandes", "x", "licence_creer", "demandes"}) == std::vector<std::string>{"licence_creer", "demandes"},
              "normaliser : l'ordre, sans doublon ni inconnu");
        check(adm::rightLabel("licence_creer").find("signer une licence") != std::string::npos, "le libelle d'un droit");
        check(adm::actionType("signature_refusee") && adm::actionType("signature_refusee")->service && adm::actionType("licence_revoquer")->level == adm::Level::Critical,
              "les types d'action du journal");
    }

    Fixture f(source, "admins");
    check(f.ready(), "le poste d'essai : la cle d'essai (l'editeur), le registre d'exemple");
    const lic::SigningKey* editor = f.ws->signingKey();
    auto lea = makeKey(f.root / "cle-lea", "Lea essai");
    auto hugo = makeKey(f.root / "cle-hugo", "Hugo essai");
    if (!editor || !lea || !hugo) return finish();

    std::printf("-- l'empreinte d'une cle\n");
    {
        const std::string fp = adm::fingerprint(lea->publicKeyHex());
        check(fp.size() == 29 && fp[4] == ' ' && fp[24] == ' ', "6 groupes de 4 : " + fp);
        check(std::none_of(fp.begin(), fp.end(), [](char c) { return c >= 'a' && c <= 'z'; }), "en majuscules");
        check(fp == adm::fingerprint(lea->publicKeyHex()) && fp != adm::fingerprint(hugo->publicKeyHex()), "stable, une par cle");
        check(adm::fingerprint("pas une cle").empty(), "pas une cle : vide");
    }

    std::printf("-- la liste des admins\n");
    {
        adm::AdminList l;
        l.number = 3;
        l.issued = g_now;
        l.admins = {adminOf(*lea, "L\xC3\xA9" "a", {"licence_creer", "demandes"}), adminOf(*hugo, "Hugo", {"licence_revoquer"})};
        const std::string text = adm::writeAdminList(l);
        adm::AdminList back;
        std::string why;
        check(adm::parseAdminList(text, back, &why) && back.number == 3 && back.admins.size() == 2 && back.admins[0].name == "L\xC3\xA9" "a" &&
                  back.admins[0].rights == l.admins[0].rights && back.byKeyId(lic::keyIdOf(hugo->publicKey())),
              "ecrite, relue (" + why + ")");
        adm::AdminList bad = l;
        bad.admins[0].id = lic::keyIdOf(hugo->publicKey());
        check(!adm::parseAdminList(adm::writeAdminList(bad), back, &why) && has(why, "identifiant"), "un identifiant qui n'est pas celui de la cle : refusee");
        bad = l;
        bad.admins[1] = bad.admins[0];
        check(!adm::parseAdminList(adm::writeAdminList(bad), back, &why) && has(why, "d\xC3\xA9j\xC3\xA0"), "une cle deux fois : refusee");
        core::json::Value v;
        (void)core::json::parse(text, v);
        v["admins"].items()[0]["droits"].push("tout_faire");
        check(!adm::parseAdminList(core::json::write(v, false), back, &why) && has(why, "tout_faire"), "un droit inconnu : refusee");
        bad = l;
        bad.number = 0;
        check(!adm::parseAdminList(adm::writeAdminList(bad), back, &why), "sans numero : refusee");
    }

    std::printf("-- un document signe par un admin\n");
    {
        adm::AdminList l;
        l.number = 1;
        l.admins = {adminOf(*lea, "L\xC3\xA9" "a", {"licence_creer"})};
        std::string why;
        const adm::Admin* who = nullptr;
        check(adm::verifyAdminBlock(l, signWith(*lea, lic::DocKind::License, "{\"x\":1}"), &why, &who) && who && who->name == "L\xC3\xA9" "a",
              "Lea signe une licence : permis (" + why + ")");
        check(!adm::verifyAdminBlock(l, signWith(*lea, lic::DocKind::Registry, "{\"x\":1}"), &why) && has(why, "pas le droit"), "un registre, sans le droit : refuse");
        check(!adm::verifyAdminBlock(l, signWith(*lea, lic::DocKind::Admins, "{\"x\":1}"), &why) && has(why, "\xC3\xA9" "diteur"), "la liste des admins : jamais par un admin");
        check(!adm::verifyAdminBlock(l, signWith(*hugo, lic::DocKind::License, "{\"x\":1}"), &why) && has(why, "inconnue"), "Hugo, absent de la liste : refuse");
        auto forged = signWith(*lea, lic::DocKind::License, "{\"x\":1}");
        forged.document = "{\"x\":2}";
        check(!adm::verifyAdminBlock(l, forged, &why) && has(why, "fausse"), "un document modifie : refuse");
        l.admins[0].retired = g_now;
        l.admins[0].rights.clear();
        l.admins[0].kept = {adm::documentHash("{\"x\":1}")};
        check(adm::verifyAdminBlock(l, signWith(*lea, lic::DocKind::License, "{\"x\":1}"), &why), "retiree : un document garde vaut");
        check(!adm::verifyAdminBlock(l, signWith(*lea, lic::DocKind::License, "{\"x\":3}"), &why) && has(why, "retir"), "retiree : un autre est refuse");
    }

    std::printf("-- le PC d'un client : une licence signee par Lea\n");
    const std::string req = f.request("pc-client", "Poste essai client");
    const auto signedR = f.licence("XPG-2026-00501", "Atelier d'essai Admins", "PERSONAL", {req});
    check(signedR.ok, "l'editeur signe une licence (" + signedR.text() + ")");
    lic::SignedBlock edLic;
    check(licenceBlockOf(f.root / "sortie" / "XPG-2026-00501.xpglic", edLic), "son bloc licence");
    // la meme licence, signee par Lea (ce que ferait son Admin)
    const lic::SignedBlock leaLic = signWith(*lea, lic::DocKind::License, edLic.document);
    const std::string leaFile = lic::writeBlock(leaLic);
    lic::LicenseStore pc(f.pcOptions(f.root / "pc-client"));
    lic::SignedBlock regBlock;
    check(blockOf(f.root / "sortie" / "XPG-2026-00501.xpglic", lic::DocKind::Registry, regBlock), "le registre (signe par l'editeur) dans le fichier");
    check(pc.importBytes(lic::writeBlock(regBlock)).ok, "le PC a le registre");
    {
        const auto rep = pc.importBytes(leaFile);
        check(!rep.ok && has(rep.why, "inconnue"), "sans la liste des admins : refusee (" + rep.why + ")");
    }
    const std::string list1 = listText(*editor, 1, {adminOf(*lea, "L\xC3\xA9" "a", {"licence_creer", "demandes"})});
    {
        bool changed = false;
        std::string why;
        check(pc.adoptAdminList(list1, &changed, &why) && changed && pc.adminList() && pc.adminList()->number == 1, "la liste n. 1 gardee (" + why + ")");
        check(pc.adoptAdminList(list1, &changed, &why) && !changed, "la meme : rien a faire");
        const std::string byLea = lic::writeBlock(signWith(*lea, lic::DocKind::Admins, adm::writeAdminList(adm::AdminList{2, g_now, {}})));
        check(!pc.adoptAdminList(byLea, &changed, &why) && has(why, "\xC3\xA9" "diteur"), "une liste signee par un admin : refusee");
        const auto rep = pc.importBytes(leaFile);
        check(rep.ok && rep.licenseInstalled && rep.state.status == lic::LicenseStatus::Valid, "avec la liste : la licence de Lea est installee, valide (" + rep.why + ")");
        std::string signer;
        check(pc.verifyPublished(leaLic, &why, &signer) && signer == "L\xC3\xA9" "a", "signee par : Lea");
    }
    {
        lic::LicenseStore again(f.pcOptions(f.root / "pc-client"));
        check(again.adminList() && again.evaluate().status == lic::LicenseStatus::Valid, "relu au lancement suivant : la liste, la licence valide");
    }

    std::printf("-- la liste suivante, l'ancienne ne revient pas\n");
    const std::string list2 = listText(*editor, 2, {adminOf(*lea, "L\xC3\xA9" "a", {"licence_creer", "demandes"}), adminOf(*hugo, "Hugo", {"licence_revoquer"})});
    {
        bool changed = false;
        std::string why;
        check(pc.adoptAdminList(list2, &changed, &why) && changed && pc.adminList()->admins.size() == 2, "la n. 2 : Hugo arrive");
        check(!pc.adoptAdminList(list1, &changed, &why) && has(why, "ancienne"), "la n. 1 : plus ancienne, refusee");
        const std::string other2 = listText(*editor, 2, {adminOf(*lea, "L\xC3\xA9" "a", {"licence_creer"})});
        check(!pc.adoptAdminList(other2, &changed, &why) && has(why, "diff\xC3\xA9rentes"), "une autre n. 2 : refusee");
        // le fichier remplace a la main par l'ancienne : ignoree (l'etat protege garde le n. 2)
        std::error_code ec;
        fs::copy_file(f.root / "pc-client" / "admins.xpgadm", f.root / "admins-2.bak", fs::copy_options::overwrite_existing, ec);
        { std::ofstream o(f.root / "pc-client" / "admins.xpgadm", std::ios::binary); o << list1; }
        lic::LicenseStore rolled(f.pcOptions(f.root / "pc-client"));
        check(!rolled.adminList(), "l'ancienne liste remise a la main : ignoree");
        fs::copy_file(f.root / "admins-2.bak", f.root / "pc-client" / "admins.xpgadm", fs::copy_options::overwrite_existing, ec);
        lic::LicenseStore back(f.pcOptions(f.root / "pc-client"));
        check(back.adminList() && back.adminList()->number == 2, "la n. 2 remise : relue");
    }

    std::printf("-- un fichier a deux blocs (la liste, puis la licence), sur un PC neuf\n");
    {
        const std::string req2 = f.request("pc-neuf", "Poste neuf");
        const auto r2 = f.licence("XPG-2026-00502", "Atelier d'essai Admins", "PERSONAL", {req2});
        lic::SignedBlock b2;
        check(r2.ok && licenceBlockOf(f.root / "sortie" / "XPG-2026-00502.xpglic", b2), "une seconde licence");
        lic::LicenseStore neuf(f.pcOptions(f.root / "pc-neuf"));
        const auto rep = neuf.importBytes(list2 + lic::writeBlock(regBlock) + lic::writeBlock(signWith(*lea, lic::DocKind::License, b2.document)));
        check(rep.ok && rep.licenseInstalled && neuf.adminList() && neuf.adminList()->number == 2, "la liste puis la licence de Lea : installees (" + rep.why + ")");
        const auto rep2 = neuf.importBytes(lic::writeBlock(signWith(*hugo, lic::DocKind::License, b2.document)));
        check(!rep2.ok && has(rep2.why, "pas le droit"), "la meme, signee par Hugo (sans le droit) : refusee");
    }

    std::printf("-- Lea retiree : ce qui est garde vaut\n");
    {
        adm::Admin gone = adminOf(*lea, "L\xC3\xA9" "a", {});
        gone.retired = g_now;
        gone.kept = {adm::documentHash(edLic.document)};
        const std::string list3 = listText(*editor, 3, {gone, adminOf(*hugo, "Hugo", {"licence_revoquer"})});
        bool changed = false;
        std::string why;
        check(pc.adoptAdminList(list3, &changed, &why) && changed, "la n. 3 : Lea retiree");
        check(pc.evaluate().status == lic::LicenseStatus::Valid, "sa licence gardee : toujours valide");
        lic::License other;
        check(lic::parseLicense(edLic.document, other, &why), "relire la licence");
        other.issuedAt += 60;
        const std::string newer = lic::writeLicense(other);
        check(!pc.verifyPublished(signWith(*lea, lic::DocKind::License, newer), &why) && has(why, "retir"), "une nouvelle licence signee par Lea : refusee");
        // un droit retire (Hugo reste, sans droit) : ce qui est garde vaut encore
        adm::Admin h = adminOf(*hugo, "Hugo", {});
        const lic::SignedBlock hugoRev = signWith(*hugo, lic::DocKind::Revocations, "{\"essai\":1}");
        h.kept = {adm::documentHash("{\"essai\":1}")};
        const std::string list4 = listText(*editor, 4, {gone, h});
        check(pc.adoptAdminList(list4, &changed, &why), "la n. 4 : Hugo n'a plus de droit");
        check(pc.verifyPublished(hugoRev, &why), "sa revocation gardee : valable");
        check(!pc.verifyPublished(signWith(*hugo, lic::DocKind::Revocations, "{\"essai\":2}"), &why) && has(why, "pas le droit"), "une nouvelle : refusee");
    }

    std::printf("-- le journal des admins\n");
    {
        adm::JournalEntry e;
        e.when = g_now;
        e.action = "licence_creer";
        e.object = "XPG-2026-00501";
        e.summary = "a sign\xC3\xA9 la licence XPG-2026-00501";
        e.detail = "{\"client\":\"Atelier d'essai Admins\"}";
        e.after = "{\"offre\":\"PERSONAL\"}";
        e.pc = "PC-ESSAI";
        e.machine = std::string(32, 'b');
        e.software = "1.44.0";
        e.nonce = std::string(32, 'c');
        const std::string entry = adm::writeJournalEntry(e);
        adm::JournalEntry back;
        std::string why;
        check(adm::parseJournalEntry(entry, back, &why) && back.action == e.action && back.object == e.object && back.summary == e.summary &&
                  has(back.detail, "Atelier") && has(back.after, "PERSONAL"),
              "l'entree : ecrite, relue (" + why + ")");
        const auto sig = lea->sign(adm::journalMessage(entry));
        core::json::Value line = core::json::Value::object();
        line.set("numero", 1);
        line.set("precedent", "");
        line.set("recu", static_cast<std::int64_t>(g_now));
        line.set("cle", lea->publicKeyHex());
        line.set("entree", entry);
        line.set("signature", deploy::crypto::hex(sig));
        const std::string text = core::json::write(line, false);
        adm::JournalLine l;
        check(adm::parseJournalLine(text, l, &why) && l.number == 1 && l.hash == deploy::crypto::sha256Hex(text) && l.parsed.action == "licence_creer",
              "la ligne chainee : relue (" + why + ")");
        check(adm::verifyJournalSignature(l), "sa signature : celle de Lea");
        adm::JournalLine forged = l;
        forged.entry.replace(forged.entry.find("00501"), 5, "00999");
        check(!adm::verifyJournalSignature(forged), "une entree modifiee : la signature ne va plus");
        forged = l;
        forged.key = hugo->publicKeyHex();
        check(!adm::verifyJournalSignature(forged), "une autre cle : refusee");
        line.set("precedent", "pas-une-empreinte");
        check(!adm::parseJournalLine(core::json::write(line, false), l, &why), "un precedent mal ecrit : refusee");
    }

    std::printf("-- fichiers, versions, messages, jeton d'admin\n");
    {
        adm::FileManifest m;
        m.id = std::string(32, 'd');
        m.name = "XPG-License-Admin-Setup-1.44.0.exe";
        m.size = 6595615;
        m.sha256 = std::string(64, 'e');
        m.kind = "admin";
        m.version = "1.44.0";
        m.access = "admins";
        adm::FileManifest mb;
        std::string why;
        check(adm::parseFileManifest(adm::writeFileManifest(m), mb, &why) && mb.name == m.name && mb.size == m.size && mb.chunks() == 7, "le manifeste (7 morceaux)");
        for (const char* bad : {"../x.exe", ".cache", "a/b.exe", "nom\\x.exe", ""}) {
            adm::FileManifest x = m;
            x.name = bad;
            check(!adm::parseFileManifest(adm::writeFileManifest(x), mb, &why), std::string("un nom de fichier refuse : ") + bad);
        }
        adm::VersionManifest v;
        v.version = "1.44.1";
        v.published = g_now;
        v.notes = {"Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es."};
        v.files = {{m.id, "API-IHM-Studio-Setup-1.44.1.exe", m.sha256, "studios", "tous", 57000000},
                   {std::string(32, 'f'), "XPG-License-Admin-Setup-1.44.1.exe", m.sha256, "admin", "admins", 6600000}};
        adm::VersionManifest vb;
        check(adm::parseVersion(adm::writeVersion(v), vb, &why) && vb.version == "1.44.1" && vb.file("admin") && vb.file("studios")->audience == "tous", "la version");
        check(adm::compareSemver("1.44.10", "1.44.9") > 0 && adm::compareSemver("1.43.0", "1.44.0") < 0 && adm::compareSemver("1.44.0", "1.44.0") == 0,
              "comparer les versions");
        check(adm::compareSemver("pas une", "1.0.0") < 0, "une version illisible : la plus petite");
        adm::MessageContent c;
        c.text = "Installe l'Admin.";
        c.action = "admin";
        c.files = {{m.id, m.name, m.sha256, "1.44.0", std::string(32, '1'), m.size}};
        adm::MessageContent cb;
        check(adm::parseMessageContent(adm::writeMessageContent(c), cb, &why) && cb.files.size() == 1 && cb.files[0].token == c.files[0].token && cb.action == "admin",
              "le contenu d'un message");
        adm::Message msg;
        msg.device = std::string(32, '2');
        msg.title = "Camille t'a ajout\xC3\xA9" "e comme admin";
        msg.from = "Camille";
        msg.sealed = "c2NlbGxl";
        adm::Message mbk;
        check(adm::parseMessage(adm::writeMessage(msg), mbk, &why) && mbk.title == msg.title && mbk.device == msg.device, "le message");
        adm::AdminToken t{lea->publicKeyHex(), std::string(32, '3'), g_now + 30 * lic::kDay};
        adm::AdminToken tb;
        check(adm::parseAdminToken(adm::writeAdminToken(t), tb, &why) && tb.key == t.key && tb.expires == t.expires, "le jeton d'admin");
    }

    std::printf("-- les profils\n");
    {
        std::vector<adm::Profile> p = adm::defaultProfiles();
        check(p.size() == 1 && p[0].builtin && p[0].rights.size() == 13, "au depart : Tous les droits");
        p.push_back({"Admin LV1", {"licence_creer", "demandes"}, false, g_now});
        p.push_back({"Admin LV2", {"licence_creer", "licence_prolonger", "offres", "demandes"}, false, g_now});
        std::vector<adm::Profile> back;
        std::string why;
        check(adm::parseProfiles(adm::writeProfiles(p), back, &why) && back.size() == 3 && back[0].builtin && back[1].name == "Admin LV1" && back[2].rights.size() == 4,
              "ecrits, relus (le profil de depart en tete)");
    }
    return finish();
}
