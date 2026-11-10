// =============================================================================
//  tests/admin_gui_adminsvue_test.cpp - 1.47.0 : LES PAGES DES ADMINS (maquette
//  1.47.0, planches 01 a 05) - dessinees a blanc (TextRenderer, HostRig)
// -----------------------------------------------------------------------------
//  Un poste d'admin+ d'essai (Camille Essai) : la liste signee (Lea Essai, Hugo
//  Essai, Nina Essai retiree), un journal des admins SIGNE ET CHAINE sur 80 jours
//  (des actions de chaque type, un pic, des heures inhabituelles, un autre PC, des
//  signatures refusees, des droits changes, les gestes reserves de l'admin+), des
//  licences dans l'archive ; les reponses du service : celles d'essai de StatsKit
//  (sampleFetchers : des JSON lus par les vrais lecteurs). Rien de reel.
//   - la vue d'ensemble : chaque bloc (les tuiles, le classement, la ligne
//     depliee, les alertes et leur geste), la periode (7 j, 30 j, 90 j, 12 mois,
//     tout), le tri par colonne, la barre de filtres, le clic droit dans l'ordre
//     de toutes les listes ;
//   - qui fait quoi : les parts, les petits graphiques, ce que l'admin+ seul fait ;
//   - la fiche : chaque ancre (vue d'ensemble, activite, licences et clients,
//     demandes, boutique, securite, droits), << Retirer ce droit ? >> ;
//   - un admin : la lecture seule, << Ma fiche >> et la sienne seulement ;
//   - le chargement, un service d'avant la 1.47.0 (le bandeau, Deployer), un
//     poste sans journal.
//  --generer DOSSIER : le meme poste, au jour d'aujourd'hui, pour les captures
//  sous Wine (DOSSIER/poste, et DOSSIER/cle.xpgcle : la cle de l'editeur
//  sauvegardee avec la phrase de la variable XPG_PHRASE_ESSAI ; la cle de ce PC
//  Linux n'y est pas : restaurer-cle la remet sous Windows).
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/JournalStats.hpp"
#include "../src/license_admin/KeyBackup.hpp"
#include "../src/license_admin/gui/AdminsBoard.hpp"
#include "../src/license_admin/gui/StatsKit.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace admintest;
namespace adm = licensing::admins;
namespace json = core::json;
namespace la = license_admin;
namespace dc = deploy::crypto;
namespace js = license_admin::journalstats;
namespace ab = license_admin::gui::adminsboard;
namespace sk = license_admin::gui::statskit;

namespace {

const std::string kLea = "L\xC3\xA9" "a Essai";

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string draw(ui::Widget& w, float width = 1141.f, float height = 813.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}
std::string signedList(const lic::SigningKey& editor, const adm::AdminList& list) {
    return lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(list), editor.publicKey(),
                                          [&editor](std::string_view m) { return editor.sign(m); }));
}
void pinEditor(const fs::path& folder, const lic::SigningKey& editor) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editor.publicKeyHex() + "\"]}\n");
}

// ---- la chaine du journal des admins ----
struct Event {
    lic::Seconds          when{0};
    const lic::SigningKey* key{nullptr};
    std::string           action, object, summary, detail, before, after, pc, machine;
};
struct Chain {
    std::uint64_t            n{0};
    std::string              prev;
    std::vector<std::string> lines;
};
void addLine(Chain& c, const Event& ev) {
    adm::JournalEntry e;
    e.when = ev.when;
    e.action = ev.action;
    e.object = ev.object;
    e.summary = ev.summary;
    e.detail = ev.detail;
    e.before = ev.before;
    e.after = ev.after;
    e.pc = ev.pc;
    e.machine = ev.machine;
    e.software = "XPG-License-Admin 1.47.0";
    char nonce[40];
    std::snprintf(nonce, sizeof nonce, "%032llx", static_cast<unsigned long long>(c.n * 2654435761ull + 17));
    e.nonce = nonce;
    const std::string entry = adm::writeJournalEntry(e);
    json::Value o = json::Value::object();
    o.set("numero", static_cast<std::uint64_t>(++c.n));
    o.set("precedent", c.prev);
    o.set("recu", static_cast<std::int64_t>(ev.when + 2));
    o.set("cle", ev.key->publicKeyHex());
    o.set("entree", entry);
    o.set("signature", dc::hex(ev.key->sign(adm::journalMessage(entry))));
    const std::string line = json::write(o, false);
    c.lines.push_back(line);
    c.prev = dc::sha256Hex(line);
}
void storeChain(const fs::path& folder, const Chain& c, const std::string& serviceKey) {
    std::string text;
    for (const auto& l : c.lines) text += l + "\n";
    writeFile(folder / "journal-admins.jsonl", text);
    writeFile(folder / "journal-admins-etat.json", "{\"format\":\"xpg-journal-admins/1\",\"dernier\":" + std::to_string(c.n) + ",\"empreinte\":\"" + c.prev +
                                                      "\",\"cle_service\":\"" + serviceKey + "\",\"problemes\":[]}\n");
}

// Un generateur stable (les memes donnees a chaque passage).
struct Rng {
    std::uint64_t s{0x9E3779B97F4A7C15ull};
    unsigned next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(s >> 33);
    }
    int upTo(int n) { return n > 0 ? static_cast<int>(next() % static_cast<unsigned>(n)) : 0; }
};

// ============================================================================ le poste d'essai
struct Poste {
    std::unique_ptr<Fixture>         fx;
    std::unique_ptr<lic::SigningKey> lea, hugo, nina, service;
    std::vector<std::string>         renew;   // les licences de Lea qui finissent dans les 30 jours
    int                              leaLines{0};
};

bool buildPoste(Poste& p, const std::string& source, const std::string& tag) {
    p.fx = std::make_unique<Fixture>(source, tag);
    Fixture& fx = *p.fx;
    if (!fx.ws->createKey("Camille Essai", true).ok || !fx.ready()) return false;
    std::string why;
    p.lea = la::createAdminKey(U(fx.root / "lea"), kLea, &why);
    p.hugo = la::createAdminKey(U(fx.root / "hugo"), "Hugo Essai", &why);
    p.nina = la::createAdminKey(U(fx.root / "nina"), "Nina Essai", &why);
    p.service = la::createAdminKey(U(fx.root / "service"), "le service", &why);
    if (!p.lea || !p.hugo || !p.nina || !p.service || !fx.ws->signingKey()) return false;
    const lic::SigningKey& editor = *fx.ws->signingKey();
    const js::TimeZone tz = js::TimeZone::paris();
    const std::int64_t today = js::localDay(g_now, tz);
    const auto at = [&](std::int64_t day, int h, int m) { return js::dayStart(day, tz) + h * 3600 + m * 60; };
    const std::int64_t leaSince = today - 57, hugoSince = today - 120, ninaSince = today - 200, ninaGone = today - 13;
    // ---- la liste signee, les profils ----
    adm::AdminList list;
    list.number = 5;
    list.issued = at(today - 5, 9, 0);
    const auto admin = [](const lic::SigningKey& k, const std::string& name, const std::string& pc, const std::string& machine, const std::string& profile,
                          std::vector<std::string> rights, lic::Seconds since, lic::Seconds retired) {
        adm::Admin a;
        a.id = k.keyId();
        a.key = k.publicKeyHex();
        a.name = name;
        a.pc = pc;
        a.machine = machine;
        a.since = since;
        a.profile = profile;
        a.rights = adm::normalizeRights(rights);
        a.retired = retired;
        return a;
    };
    const std::vector<std::string> lv2{"licence_creer", "licence_prolonger", "licence_option", "licence_revoquer", "poste_retirer",
                                       "annonces",      "demandes",          "codes",          "client_supprimer", "courriels"};
    std::vector<std::string> leaRights = lv2;
    leaRights.erase(std::remove(leaRights.begin(), leaRights.end(), "licence_prolonger"), leaRights.end());
    leaRights.push_back("clients_exporter");
    list.admins.push_back(admin(*p.lea, kLea, "PC-COMMERCIAL-02", "4f2a9c1b77d0e35a4f2a9c1b77d0e35a", "Admin LV2", leaRights, at(leaSince, 10, 0), 0));
    list.admins.push_back(admin(*p.hugo, "Hugo Essai", "PORTABLE-HUGO", "8b31d04e5a2c9f178b31d04e5a2c9f17", "Admin LV2", lv2, at(hugoSince, 10, 0), 0));
    list.admins.push_back(admin(*p.nina, "Nina Essai", "PC-ATELIER-04", "c07e6d2a91f3b548c07e6d2a91f3b548", "Admin LV1", {"licence_creer", "demandes", "courriels"},
                                at(ninaSince, 10, 0), at(ninaGone, 16, 2)));
    writeFile(fx.admin / "admins.xpgadm", signedList(editor, list));
    adm::Profile lv2p;
    lv2p.name = "Admin LV2";
    lv2p.rights = lv2;
    (void)fx.ws->saveProfile(lv2p);
    adm::Profile lv1p;
    lv1p.name = "Admin LV1";
    lv1p.rights = {"licence_creer", "demandes", "courriels"};
    (void)fx.ws->saveProfile(lv1p);
    // ---- des licences dans l'archive : celles de Lea qui finissent bientot (A renouveler) ----
    const char* const customers[4] = {"Laboratoire d'essai", "Client d'essai", "Bureau d'\xC3\xA9tudes Exemple", "Studio Exemple"};
    const char* const editions[4] = {"PROFESSIONAL", "PERSONAL", "ENTERPRISE", "PERSONAL"};
    const int durations[4] = {8, 14, 21, 27};
    for (int i = 0; i < 4; ++i) {
        char id[32];
        std::snprintf(id, sizeof id, "XPG-2026-%05d", 231 + i * 13);
        if (fx.licence(id, customers[i], editions[i], {fx.request("pc-r" + std::to_string(i), "ESSAI-PC" + std::to_string(i))}, durations[i]).ok) p.renew.push_back(id);
    }
    fx.ws->reload();
    // ---- le journal : 80 jours ----
    Rng rng;
    std::vector<Event> evs;
    int licenceNo = 400;
    const char* const clients[6] = {"Atelier Exemple", "Soci\xC3\xA9t\xC3\xA9 d'essai", "Usine Exemple Nord", "Bureau d'\xC3\xA9tudes Exemple", "Client d'essai",
                                    "Laboratoire Exemple"};
    const auto gesture = [&](const lic::SigningKey& k, lic::Seconds when, const std::string& pc, const std::string& machine, int kind) {
        Event e;
        e.when = when;
        e.key = &k;
        e.pc = pc;
        e.machine = machine;
        const std::string client = clients[rng.upTo(6)];
        char lid[32];
        std::snprintf(lid, sizeof lid, "XPG-2026-%05d", ++licenceNo);
        switch (kind) {
            case 0:
                e.action = "licence_creer";
                e.object = lid;
                e.summary = "a sign\xC3\xA9 la licence " + e.object;
                e.detail = "{\"client\":\"" + client + "\",\"offre\":\"PROFESSIONAL 1 an\"}";
                break;
            case 1:
                e.action = "licence_prolonger";
                e.object = lid;
                e.summary = "a prolong\xC3\xA9 la licence " + e.object;
                break;
            case 2:
                e.action = "licence_option";
                e.object = lid;
                e.summary = "a ajout\xC3\xA9 Exports avanc\xC3\xA9s \xC3\xA0 " + e.object;
                break;
            case 3: {
                e.action = rng.upTo(6) == 0 ? "demande_refuser" : "demande_accepter";
                const int n = 40 + rng.upTo(60);
                e.object = "demande " + std::to_string(n);
                e.summary = std::string(e.action == "demande_refuser" ? "a refus\xC3\xA9" : "a accept\xC3\xA9") + " la demande n\xC2\xB0 " + std::to_string(n) + " (" + client + ")";
                break;
            }
            case 4:
                e.action = "courriel";
                e.object = "contact@exemple.fr";
                e.summary = "a \xC3\xA9" "crit \xC3\xA0 " + client;
                break;
            case 5: {
                e.action = "commande_livrer";
                const int n = 150 + rng.upTo(80);
                e.object = "commande " + std::to_string(n);
                e.summary = "a livr\xC3\xA9 la commande n\xC2\xB0 " + std::to_string(n);
                break;
            }
            case 6:
                e.action = "code_revoir";
                e.object = lid;
                e.summary = "a revu le code de " + e.object;
                break;
            default:
                e.action = "poste_retirer";
                e.object = lid;
                e.summary = "a retir\xC3\xA9 un PC de " + e.object;
                break;
        }
        evs.push_back(std::move(e));
    };
    const std::string leaPc = "PC-COMMERCIAL-02", leaM = "4f2a9c1b77d0e35a4f2a9c1b77d0e35a";
    const std::string hugoPc = "PORTABLE-HUGO", hugoM = "8b31d04e5a2c9f178b31d04e5a2c9f17";
    const std::string ninaPc = "PC-ATELIER-04", ninaM = "c07e6d2a91f3b548c07e6d2a91f3b548";
    const std::string plusPc = "PC-BUREAU-01";
    std::int64_t peak = today - 11;
    while (js::weekday(peak) >= 5) --peak;
    const std::int64_t absent1 = today - 22, absent2 = today - 21;
    for (std::int64_t d = today - 80; d <= today; ++d) {
        const int wd = js::weekday(d);
        const bool weekday = wd < 5;
        const int maxMinute = d == today ? std::max(0, js::localMinuteOfDay(g_now, tz) - 30) : 1080;
        const auto minuteIn = [&](int lo, int hi) { return std::min(lo + rng.upTo(hi - lo), std::max(lo, maxMinute)); };
        // Lea : chaque jour ouvre (sauf deux), 3 a 7 actions ; un pic
        if (d >= leaSince && weekday && d != absent1 && d != absent2) {
            const int n = d == peak ? 19 : 3 + rng.upTo(5);
            for (int k = 0; k < n; ++k) {
                const int mn = minuteIn(520, 1070);
                const int roll = rng.upTo(100);
                int kind = roll < 30 ? 0 : roll < 52 ? 3 : roll < 62 ? 2 : roll < 74 ? 4 : roll < 90 ? 5 : 6;
                if (d == peak) kind = 0;
                if (kind == 6 && rng.upTo(2)) kind = 0;
                gesture(*p.lea, at(d, mn / 60, mn % 60), leaPc, leaM, kind);
                ++p.leaLines;
            }
            if (d < today - 8 && rng.upTo(3) == 0) {   // prolonger : avant que ce droit lui soit retire
                gesture(*p.lea, at(d, 11, 30), leaPc, leaM, 1);
                ++p.leaLines;
            }
        }
        // Hugo : un jour ouvre sur deux ou trois
        if (d >= hugoSince && weekday && rng.upTo(10) < 6) {
            const int n = 1 + rng.upTo(3);
            for (int k = 0; k < n; ++k) {
                const int mn = minuteIn(540, 1020);
                const int roll = rng.upTo(100);
                gesture(*p.hugo, at(d, mn / 60, mn % 60), hugoPc, hugoM, roll < 30 ? 0 : roll < 55 ? 3 : roll < 70 ? 2 : roll < 85 ? 5 : roll < 93 ? 4 : 7);
            }
        }
        // Nina : jusqu'a son retrait
        if (d >= ninaSince && d < ninaGone && weekday && rng.upTo(10) < 4) gesture(*p.nina, at(d, 10 + rng.upTo(6), rng.upTo(60)), ninaPc, ninaM, rng.upTo(2) ? 0 : 3);
        // Camille (l'admin+)
        if (weekday && rng.upTo(10) < 7) {
            const int n = 1 + rng.upTo(3);
            for (int k = 0; k < n; ++k) {
                const int mn = minuteIn(480, 1080);
                const int roll = rng.upTo(100);
                gesture(editor, at(d, mn / 60, mn % 60), plusPc, {}, roll < 35 ? 0 : roll < 60 ? 3 : roll < 80 ? 5 : roll < 90 ? 2 : 4);
            }
        }
        // les sauvegardes de chaque nuit (ecrites par le service, a la demande de l'admin+)
        if (d >= today - 30 && d < today) evs.push_back({at(d, 3, 0), p.service.get(), "sauvegarde", "sauvegarde", "a fait la sauvegarde de secours", "{}", {}, {}, {}, {}});
    }
    // les gestes qui comptent (la maquette)
    const auto add = [&](lic::Seconds when, const lic::SigningKey& k, const std::string& action, const std::string& object, const std::string& summary,
                         const std::string& detail = {}, const std::string& pc = {}, const std::string& machine = {}, const std::string& before = {},
                         const std::string& after = {}) { evs.push_back({when, &k, action, object, summary, detail, before, after, pc, machine}); };
    add(at(leaSince, 10, 0), editor, "admin_ajouter", p.lea->keyId(), "a ajout\xC3\xA9 l'admin " + kLea, "{\"nom\":\"" + kLea + "\",\"profil\":\"Admin LV2\"}", plusPc);
    add(at(today - 8, 10, 0), editor, "admin_droits", p.lea->keyId(), "a chang\xC3\xA9 les droits de " + kLea, {}, plusPc, {},
        "{\"droits\":[\"licence_creer\",\"licence_prolonger\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\","
        "\"client_supprimer\",\"courriels\"]}",
        "{\"droits\":[\"licence_creer\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\",\"client_supprimer\","
        "\"courriels\"]}");
    add(at(today - 5, 9, 30), editor, "admin_droits", p.lea->keyId(), "a chang\xC3\xA9 les droits de " + kLea, {}, plusPc, {},
        "{\"droits\":[\"licence_creer\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\",\"client_supprimer\","
        "\"courriels\"]}",
        "{\"droits\":[\"licence_creer\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\",\"client_supprimer\","
        "\"clients_exporter\",\"courriels\"]}");
    add(at(ninaGone, 16, 2), editor, "admin_retirer", p.nina->keyId(), "a retir\xC3\xA9 l'admin Nina Essai", {}, plusPc);
    add(at(today - 6, 18, 5), editor, "version_publier", "1.47.0", "a publi\xC3\xA9 la version 1.47.0", {}, plusPc);
    add(at(today - 21, 17, 40), editor, "version_publier", "1.46.2", "a publi\xC3\xA9 la version 1.46.2", {}, plusPc);
    add(at(today - 18, 11, 0), editor, "criticite", "client_supprimer", "a r\xC3\xA9gl\xC3\xA9 la criticit\xC3\xA9 de Supprimer un client : Haute", {}, plusPc);
    add(at(today - 18, 11, 2), editor, "criticite", "code_revoir", "a r\xC3\xA9gl\xC3\xA9 la criticit\xC3\xA9 de Revoir le code : Haute", {}, plusPc);
    add(at(today - 25, 15, 0), editor, "stripe_brancher", "Stripe", "a branch\xC3\xA9 Stripe", {}, plusPc);
    add(at(today, 9, 48), editor, "licence_revoquer", "XPG-2026-00377", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2026-00377", {}, plusPc);
    // Lea : deux suppressions, deux revocations, un export, des heures inhabituelles, un autre PC
    add(at(today - 9, 14, 10), *p.lea, "client_supprimer", "Client d'essai 14", "a supprim\xC3\xA9 le client Client d'essai 14", {}, leaPc, leaM);
    add(at(today - 3, 15, 20), *p.lea, "client_supprimer", "Studio d'essai 2", "a supprim\xC3\xA9 le client Studio d'essai 2", {}, leaPc, leaM);
    add(at(today - 15, 10, 5), *p.lea, "licence_revoquer", "XPG-2026-00359", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2026-00359", {}, leaPc, leaM);
    add(at(today - 4, 11, 40), *p.lea, "licence_revoquer", "XPG-2025-00342", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2025-00342", {}, leaPc, leaM);
    add(at(today - 4, 16, 0), *p.lea, "clients_exporter", "clients", "a export\xC3\xA9 les clients (1 240 lignes)", {}, leaPc, leaM);
    std::int64_t sunday = today - 1;
    while (js::weekday(sunday) != 6) --sunday;
    const std::int64_t leaSunday = sunday - 7;
    add(at(leaSunday, 22, 14), *p.lea, "demande_accepter", "demande 51", "a accept\xC3\xA9 la demande n\xC2\xB0 51 (Client d'essai)", {}, leaPc, leaM);
    add(at(leaSunday, 22, 16), *p.lea, "code_revoir", "XPG-2026-00290", "a revu le code de XPG-2026-00290", {}, leaPc, leaM);
    add(at(today - 17, 19, 5), *p.lea, "licence_creer", "XPG-2026-00388", "a sign\xC3\xA9 la licence XPG-2026-00388", "{\"client\":\"Atelier Exemple\"}", "PC-MAISON-LEA",
        "9d9d9d9d9d9d9d9d9d9d9d9d9d9d9d9d");
    for (int i = 0; i < 4 && i < static_cast<int>(p.renew.size()); ++i)
        add(at(today - 40 + i, 10, 15), *p.lea, "licence_creer", p.renew[static_cast<std::size_t>(i)], "a sign\xC3\xA9 la licence " + p.renew[static_cast<std::size_t>(i)],
            std::string("{\"client\":\"") + customers[i] + "\"}", leaPc, leaM);
    // Hugo : 4 actions un dimanche soir (23:40 a 23:58)
    for (int k = 0; k < 4; ++k)
        add(at(sunday, 23, 40 + k * 6), *p.hugo, "licence_option", "XPG-2026-0040" + std::to_string(k), "a ajout\xC3\xA9 Exports avanc\xC3\xA9s \xC3\xA0 XPG-2026-0040" + std::to_string(k),
            {}, hugoPc, hugoM);
    // les signatures refusees (ecrites par le service) : Lea (un droit manquant), Nina (sa cle retiree)
    add(at(today - 6, 10, 2), *p.service, "signature_refusee", p.lea->keyId(), "a refus\xC3\xA9 un appel sign\xC3\xA9 par " + kLea,
        "{\"cle\":\"" + p.lea->publicKeyHex() + "\",\"nom\":\"" + kLea + "\",\"route\":\"PUT /admin/licences\",\"raison\":\"droit\",\"droit\":\"licence_prolonger\"}");
    for (int k = 0; k < 3; ++k)
        add(at(today - 2 + (k == 2 ? 2 : 0), 8, 12 + k), *p.service, "signature_refusee", p.nina->keyId(), "a refus\xC3\xA9 un appel sign\xC3\xA9 par Nina Essai (admin retir\xC3\xA9)",
            "{\"cle\":\"" + p.nina->publicKeyHex() + "\",\"nom\":\"Nina Essai\",\"route\":\"GET /admin/demandes\",\"raison\":\"cle\"}");
    std::stable_sort(evs.begin(), evs.end(), [](const Event& a, const Event& b) { return a.when < b.when; });
    Chain chain;
    for (const auto& e : evs)
        if (e.when <= g_now) addLine(chain, e);
    storeChain(fx.admin, chain, p.service->publicKeyHex());
    fx.ws->reload();
    return true;
}

ab::ScrolledView* view(ui::Widget& root, std::string_view id) { return dynamic_cast<ab::ScrolledView*>(root.findById(id)); }
std::size_t indexOfPrefix(const std::vector<std::string>& v, const std::string& prefix) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].rfind(prefix, 0) == 0) return i;
    return v.size();
}
std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : " | ") + x;
    return s;
}

// ============================================================================ --generer
int generate(const std::string& source, const std::string& out) {
    g_now = static_cast<lic::Seconds>(std::time(nullptr));
    const char* phrase = std::getenv("XPG_PHRASE_ESSAI");
    if (!phrase || !la::acceptablePassphrase(phrase, nullptr)) {
        std::printf("--generer : la variable XPG_PHRASE_ESSAI (12 caracteres au moins) donne la phrase de la sauvegarde de la cle d'essai\n");
        return 2;
    }
    Poste p;
    if (!buildPoste(p, source, "admins-vue-generer")) {
        std::printf("--generer : le poste d'essai ne se construit pas\n");
        return 1;
    }
    std::string backup, why;
    if (!la::writeKeyBackup(*p.fx->ws->signingKey(), phrase, backup, &why)) {
        std::printf("--generer : la sauvegarde de la cle : %s\n", why.c_str());
        return 1;
    }
    std::error_code ec;
    const fs::path dst = fs::path(out) / "poste";
    fs::remove_all(dst, ec);
    fs::create_directories(dst, ec);
    fs::copy(p.fx->admin, dst, fs::copy_options::recursive, ec);
    fs::remove(dst / la::kPublisherKeyFile, ec);   // la cle de ce PC : restaurer-cle la remet sous Windows
    writeFile(fs::path(out) / "cle.xpgcle", backup);
    std::printf("poste d'essai : %s (%d lignes de L\xC3\xA9" "a) ; la cle : %s\n", U(dst).c_str(), p.leaLines, U(fs::path(out) / "cle.xpgcle").c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_adminsvue_test <racine du depot> [--generer DOSSIER]\n");
        return 2;
    }
    const std::string source = argv[1];
    if (argc >= 4 && std::string(argv[2]) == "--generer") return generate(source, argv[3]);
    installMetrics();
    sk::setFetchersForTests(sk::sampleFetchers([] { return g_now; }));

    Poste P;
    check(buildPoste(P, source, "admins-vue"), "le poste d'essai : Camille (l'admin+), Lea, Hugo, Nina retiree, le journal sur 80 jours");
    if (!P.fx || !P.lea) return finish();
    Fixture& fx = *P.fx;
    {
        const auto v = fx.ws->journalLines();
        check(v.rows.size() > 300 && v.problems.empty(), "la chaine d'essai se verifie (" + std::to_string(v.rows.size()) + " lignes, " + std::to_string(v.problems.size()) + " probleme(s))");
    }
    const std::string leaKey = P.lea->publicKeyHex(), hugoKey = P.hugo->publicKeyHex(), ninaKey = P.nina->publicKeyHex();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la vue d'ensemble (planche 01) : les tuiles, le classement, la ligne depliee, les alertes\n");
    HostRig rig(fx);
    auto page = gui::makeAdminsPage(rig.host());
    rig.refreshers.push_back([&] {
        fx.ws->reload();
        page->refresh();
    });
    std::string t = draw(*page);
    auto* ov = view(*page, "admins.vue");
    check(ov && ov->visible(), "la vue d'ensemble est montree");
    check(has(t, "Administrateurs") && has(t, "3 actifs, 1 retir\xC3\xA9") && has(t, "compar\xC3\xA9 aux 30 jours d'avant"), "le titre : 3 actifs, 1 retire ; compares aux 30 jours d'avant");
    check(shown(*page, "admins.periode.7j") && shown(*page, "admins.periode.30j") && shown(*page, "admins.periode.12mois") && shown(*page, "admins.periode.tout") &&
              shown(*page, "admins.periode.dates"),
          "la periode : 7 j, 30 j, 90 j, 12 mois, tout, deux dates");
    check(shown(*page, "admins.onglet.vue") && shown(*page, "admins.onglet.qui") && shown(*page, "admins.droits-page") && shown(*page, "admins.onglet.journal"),
          "les onglets : vue d'ensemble, qui fait quoi, droits, journal");
    check(has(t, "Admins") && has(t, "actifs") && has(t, "Actions") && has(t, "Demandes en attente") && has(t, "Licences sign\xC3\xA9" "es") &&
              has(t, "Montant encaiss\xC3\xA9") && has(t, "Alertes"),
          "les six tuiles");
    check(has(t, "les admins ") && has(t, "toi ") && has(t, "commandes livr\xC3\xA9" "es"), "les tuiles du service (les reponses d'essai) : les admins et toi, les commandes");
    check(has(t, "ADMIN") && has(t, "ACTIONS") && has(t, "LICENCES") && has(t, "DEMANDES") && has(t, "D\xC3\x89LAI MOY.") && has(t, "MONTANT") && has(t, "DERNI\xC3\x88RE ACTION") &&
              has(t, "\xC3\x89TAT"),
          "le classement : ses colonnes");
    check(has(t, kLea) && has(t, "Hugo Essai") && has(t, "Nina Essai") && has(t, "Camille Essai") && has(t, "Admin+") && has(t, "Admin LV2 \xC2\xB7 2 \xC3\xA9" "carts"),
          "une ligne par admin ; l'admin+ ; le profil de Lea et ses ecarts");
    check(has(t, "1.45.2"), "la version de l'Admin de Hugo (pas a jour, le service)");
    check(ov && ab::overviewSelected(*ov) == leaKey, "la ligne depliee d'office : le premier admin (Lea)");
    check(has(t, "Ses actions par jour") && has(t, "Ses 5 derni\xC3\xA8res actions") && has(t, "Ses droits") && has(t, "10 sur 13") && (has(t, "Retirer un PC : jamais") ||
              has(t, "Annonces : jamais")),
          "la ligne depliee : sa courbe, ses 5 dernieres actions, ses droits (un droit jamais utilise)");
    check(has(t, "Signature refus\xC3\xA9" "e") && has(t, "droit manquant") && has(t, "Jours actifs") && has(t, "D\xC3\xA9lai de r\xC3\xA9ponse"), "ses chiffres : une signature refusee (droit manquant), ses jours, son delai");
    check(shown(*page, "admins.fiche") && shown(*page, "admins.voir-journal") && shown(*page, "admins.droits") && shown(*page, "admins.retirer") &&
              !enabledBtn(*page, "admins.retirer"),
          "Ouvrir sa fiche, Son journal, Ses droits..., Retirer... (grise : pas de service)");
    check(has(t, "ou double-clic, ou Entr\xC3\xA9" "e"), "l'astuce : double-clic, Entree");
    const auto& alerts = [&]() -> std::vector<std::string> {
        std::vector<std::string> out;
        for (int i = 1; i <= 12; ++i)
            if (auto* b = find<ui::Button>(*page, "admins.alerte." + std::to_string(i)); b && b->visible()) out.push_back(b->text());
        return out;
    }();
    check(has(t, "Nina Essai") && has(t, "signatures refus"), "une alerte critique : les signatures refusees de Nina");
    check(has(t, "Hugo Essai") && has(t, "heure inhabituelle"), "une alerte : Hugo, a une heure inhabituelle");
    check(has(t, "son Admin est en 1.45.2"), "une alerte : l'Admin de Hugo pas a jour");
    check(has(t, "jamais utilis"), "une alerte : des droits jamais utilises");
    check(!alerts.empty() && std::find(alerts.begin(), alerts.end(), "Voir dans le journal") != alerts.end() &&
              std::find(alerts.begin(), alerts.end(), "Lui envoyer la mise \xC3\xA0 jour") != alerts.end(),
          "chaque alerte a son geste (" + joined(alerts) + ")");
    check(has(t, "Rien \xC3\xA0 signaler"), "Rien a signaler (ce qui va bien)");
    {
        const std::size_t n0 = rig.navigations.size();
        check(click(*page, "admins.alerte.1") && rig.navigations.size() == n0 + 1, "le geste de la premiere alerte : une page (" +
                                                                                       (rig.navigations.empty() ? std::string() : rig.navigations.back()) + ")");
        (void)gui::takePageFocus("journal");
        (void)gui::takePageFocus("droits");
    }

    std::printf("-- la periode : 7 j, 90 j, 12 mois, tout ; la periode d'avant\n");
    {
        check(click(*page, "admins.periode.7j"), "7 j");
        t = draw(*page);
        check(has(t, "compar\xC3\xA9 aux 7 jours d'avant"), "7 j : compares aux 7 jours d'avant");
        check(click(*page, "admins.periode.90j"), "90 j");
        t = draw(*page);
        check(has(t, "compar\xC3\xA9 aux 90 jours d'avant"), "90 j : compares aux 90 jours d'avant");
        check(click(*page, "admins.periode.12mois"), "12 mois");
        t = draw(*page);
        check(has(t, "compar\xC3\xA9 aux 12 mois d'avant"), "12 mois");
        check(click(*page, "admins.periode.tout"), "tout");
        t = draw(*page);
        check(has(t, "compar\xC3\xA9 \xC3\xA0 la p\xC3\xA9riode d'avant"), "tout : compare a la periode d'avant");
        check(click(*page, "admins.periode.dates"), "deux dates");
        t = draw(*page);
        check(shown(*page, "admins.du") && shown(*page, "admins.au"), "deux dates : du, au");
        check(click(*page, "admins.periode.30j"), "de nouveau 30 j");
        t = draw(*page);
        check(has(t, "compar\xC3\xA9 aux 30 jours d'avant"), "30 j");
    }

    std::printf("-- le classement : le tri, la barre de filtres, deplier, le clic droit\n");
    if (ov) {
        const auto order = ab::overviewOrder(*ov);
        check(order.size() == 4 && order.front() == leaKey, "trie par actions : Lea d'abord (" + std::to_string(order.size()) + " lignes)");
        ab::overviewSort(*ov, 0);
        auto byName = ab::overviewOrder(*ov);
        check(!byName.empty() && byName.front() == fx.ws->signingKey()->publicKeyHex(), "par admin (A a Z) : Camille d'abord");
        ab::overviewSort(*ov, 0);
        byName = ab::overviewOrder(*ov);
        check(!byName.empty() && byName.front() == ninaKey, "un second clic : l'ordre inverse (Nina d'abord)");
        ab::overviewSort(*ov, 1);
        auto* bar = find<ui::FilterBar>(*page, "admins.barre");
        check(bar && bar->countText() == "4 sur 4", "la barre : 4 sur 4");
        if (bar) bar->filterOn(1, "Retir\xC3\xA9");
        check(ab::overviewOrder(*ov) == std::vector<std::string>{ninaKey}, "Etat : Retire -> Nina seule");
        if (bar) bar->clearField(1);
        if (bar) bar->setSearch("PORTABLE");
        check(ab::overviewOrder(*ov) == std::vector<std::string>{hugoKey}, "chercher un PC : PORTABLE -> Hugo");
        if (bar) bar->setSearch("");
        check(ab::overviewOrder(*ov).size() == 4, "tout de nouveau");
        ab::overviewSelect(*ov, hugoKey);
        t = draw(*page);
        check(ab::overviewSelected(*ov) == hugoKey && has(t, "Ses actions par jour"), "deplier Hugo");
        // le clic droit : l'ordre de toutes les listes
        const auto m = ab::overviewMenu(*ov, hugoKey);
        const std::size_t iOpen = indexOfPrefix(m, "Ouvrir sa fiche"), iFold = indexOfPrefix(m, "Replier"), iAct = indexOfPrefix(m, "Ses droits"),
                          iGo = indexOfPrefix(m, "Aller \xC3\xA0 > Son journal"), iFil = indexOfPrefix(m, "Filtrer sur > Cet \xC3\xA9tat : Actif"),
                          iCopy = indexOfPrefix(m, "Copier > Son empreinte"), iExp = indexOfPrefix(m, "Exporter"), iDanger = indexOfPrefix(m, "Retirer cet admin");
        check(iOpen == 0 && iOpen < iFold && iFold < iAct && iAct < iGo && iGo < iFil && iFil < iCopy && iCopy < iExp && iExp < iDanger && iDanger < m.size(),
              "le clic droit : Ouvrir, Replier, Ses droits, Aller a, Filtrer sur, Copier, Exporter, Retirer (" + joined(m) + ")");
        check(indexOfPrefix(m, "Filtrer sur > Ce profil : Admin LV2") < m.size() && indexOfPrefix(m, "Filtrer sur > Cette version : 1.45.2") < m.size(),
              "Filtrer sur : son profil, sa version");
        const auto mn = ab::overviewMenu(*ov, ninaKey);
        check(indexOfPrefix(mn, "Retirer cet admin") == mn.size() && indexOfPrefix(mn, "Ses droits") == mn.size(), "Nina (retiree) : ni Retirer, ni Ses droits");
        const auto mp = ab::overviewMenu(*ov, fx.ws->signingKey()->publicKeyHex());
        check(indexOfPrefix(mp, "Retirer cet admin") == mp.size() && indexOfPrefix(mp, "Ouvrir sa fiche") == 0, "l'admin+ : Ouvrir sa fiche, pas de Retirer");
        const std::size_t n0 = rig.navigations.size();
        check(ab::overviewMenuRun(*ov, hugoKey, "Aller \xC3\xA0 > Son journal") && rig.navigations.size() == n0 + 1 && rig.navigations.back() == "journal" &&
                  gui::takePageFocus("journal") == "admin:" + hugoKey,
              "Aller a > Son journal : le journal de Hugo");
        check(ab::overviewMenuRun(*ov, hugoKey, "Filtrer sur > Ce profil : Admin LV2") && ab::overviewOrder(*ov).size() == 2, "Filtrer sur > Ce profil : Lea et Hugo");
        if (bar) bar->clearAll();
        ab::overviewSelect(*ov, leaKey);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- qui fait quoi (planche 05)\n");
    {
        check(click(*page, "admins.onglet.qui"), "l'onglet Qui fait quoi");
        t = draw(*page);
        auto* who = view(*page, "admins.qui");
        check(who && who->visible() && ov && !ov->visible(), "qui fait quoi montre, la vue d'ensemble cachee");
        check(has(t, "La part de chacun, par type d'action") && has(t, "Toutes (hors r\xC3\xA9serv\xC3\xA9" "es)") && has(t, "Cr\xC3\xA9" "er et signer une licence") &&
                  has(t, "Boutique : livrer, revoir"),
              "la part de chacun : Toutes, puis les types dans l'ordre (la boutique a la fin)");
        check(has(t, "TYPE D'ACTION") && has(t, "PART DE CHACUN") && has(t, "VS AVANT") && has(t, "EN T\xC3\x8ATE"), "ses colonnes");
        check(has(t, "Cette p\xC3\xA9riode et la p\xC3\xA9riode d'avant") && has(t, "Licences sign\xC3\xA9" "es") && has(t, "Demandes trait\xC3\xA9" "es"),
              "cette periode et celle d'avant : les petits graphiques");
        check(has(t, "Toi, l'admin+ : ce que toi seul fais") && has(t, "Versions publi\xC3\xA9" "es") && has(t, "1.47.0 le ") && has(t, "Sauvegardes de secours") &&
                  has(t, "Admins ajout\xC3\xA9s, retir\xC3\xA9s") && has(t, "Droits et profils chang\xC3\xA9s") && has(t, "Le certificat du service"),
              "toi, l'admin+ : les versions, les sauvegardes, les admins, les droits, le certificat du service");
        auto* qbar = find<ui::FilterBar>(*page, "admins.qui.barre");
        check(qbar && qbar->countText() == "15 sur 15", "la barre : 15 lignes (Toutes et les 14 types)");
        if (qbar) qbar->setSearch("Prolonger");
        check(qbar && qbar->shownCount() == 1, "chercher un type : Prolonger");
        if (qbar) qbar->setSearch("");
        if (qbar) qbar->setValues(3, {"Boutique"});
        check(qbar && qbar->shownCount() == 1, "la famille : Boutique");
        if (qbar) qbar->clearAll();
        check(click(*page, "admins.onglet.vue"), "retour a la vue d'ensemble");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la fiche (planches 02 a 04)\n");
    {
        check(click(*page, "admins.fiche"), "Ouvrir sa fiche (Lea)");
        t = draw(*page);
        auto* card = view(*page, "admins.fiche.vue");
        check(card && card->visible() && ov && !ov->visible(), "la fiche remplace la vue d'ensemble");
        check(shown(*page, "admins.fiche.retour") && shown(*page, "admins.fiche.journal") && shown(*page, "admins.fiche.droits") && shown(*page, "admins.fiche.retirer"),
              "son en-tete : retour, son journal, ses droits, retirer");
        for (int i = 0; i < 7; ++i) check(shown(*page, "admins.fiche.ancre." + std::to_string(i)), "l'ancre " + std::to_string(i));
        check(has(t, "Qui et o\xC3\xB9") && has(t, "PC-COMMERCIAL-02") && has(t, "Sa cl\xC3\xA9") && has(t, adm::fingerprint(leaKey).substr(0, 9)) &&
                  has(t, "certifi\xC3\xA9" "e \xE2\x9C\x93") && has(t, "Admin LV2, 2 \xC3\xA9" "carts"),
              "qui et ou : son PC, sa cle certifiee, son profil et ses ecarts");
        check(has(t, "Paris") || has(t, "Vue"), "vue : la ville (le service)");
        check(has(t, "S\xC3\xA9rie en cours") && has(t, "Derni\xC3\xA8re action") && has(t, "D\xC3\xA9lai de r\xC3\xA9ponse") && has(t, "jours ouvr\xC3\xA9s"), "les huit tuiles");
        check(has(t, "Ses actions par type") && has(t, "pas ce droit") && has(t, "Par criticit\xC3\xA9") && has(t, "Critique : sa") && has(t, "L\xC3\xA9" "a parmi les admins"),
              "par type (pas ce droit), par criticite (ses revocations), parmi les admins");
        check(has(t, "Quand L\xC3\xA9" "a travaille") && has(t, "Ses jours") && has(t, "D\xC3\xA9" "but et fin de ses journ\xC3\xA9" "es"), "l'activite : jour x heure, ses jours, ses journees");
        check(has(t, "Ses droits : utilis\xC3\xA9s ou non") && has(t, "DONN\xC3\x89") && has(t, "jamais"), "ses droits utilises ou non");
        check(has(t, "Sa s\xC3\xA9" "curit\xC3\xA9") && has(t, "Signatures refus\xC3\xA9" "es") && has(t, "droit manquant") && has(t, "Depuis un autre PC que le sien") &&
                  has(t, "PC-MAISON-LEA") && has(t, "Heures inhabituelles, pics") && has(t, "Ses droits chang\xC3\xA9s") && has(t, "La cha\xC3\xAEne de ses lignes") &&
                  has(t, "v\xC3\xA9rifi\xC3\xA9" "e \xE2\x9C\x93"),
              "sa securite : refusees, un autre PC, heures inhabituelles, ses droits changes, la chaine verifiee");
        check(has(t, "Les licences qu'il a sign\xC3\xA9" "es") && has(t, "Par offre") && has(t, "\xC3\x80 renouveler") && has(t, P.renew.empty() ? "?" : P.renew.front()) &&
                  has(t, "Ses clients") && has(t, "Ses 5 premiers"),
              "ce qu'il produit : ses licences, a renouveler (" + (P.renew.empty() ? std::string() : P.renew.front()) + "), ses clients");
        check(has(t, "Les demandes qu'il a trait\xC3\xA9" "es") && has(t, "Accept\xC3\xA9" "es \xC2\xB7 refus\xC3\xA9" "es") && has(t, "La boutique : les commandes qu'il a livr\xC3\xA9" "es") &&
                  has(t, "Panier moyen") && has(t, "Le reste de ce qu'il fait") && has(t, "Codes d'activation revus"),
              "les demandes, la boutique, le reste");
        // les ancres
        check(card && click(*page, "admins.fiche.ancre.1") && card->offset() > 100.f, "l'ancre Activite fait defiler");
        check(click(*page, "admins.fiche.ancre.0") && card && card->offset() < 1.f, "l'ancre Vue d'ensemble : en haut");
        // << Retirer ce droit ? >>
        const std::string unusedBtn = shown(*page, "admins.fiche.retirer.poste_retirer") ? "admins.fiche.retirer.poste_retirer" : "admins.fiche.retirer.annonces";
        const std::size_t n0 = rig.navigations.size();
        check(shown(*page, unusedBtn) && click(*page, unusedBtn) && rig.navigations.size() == n0 + 1 && rig.navigations.back() == "droits" &&
                  gui::takePageFocus("droits") == "admin:" + leaKey,
              "Retirer ce droit ? : la page Droits, Lea choisie");
        check(!rig.flashes.empty() && has(rig.flashes.back(), "d\xC3\xA9" "coche"), "... et ce qu'il faut y faire");
        check(click(*page, "admins.fiche.retour"), "retour");
        (void)draw(*page);
        check(ov && ov->visible() && !view(*page, "admins.fiche.vue"), "de nouveau la vue d'ensemble");
        // la fiche de l'admin+
        if (ov) ab::overviewSelect(*ov, fx.ws->signingKey()->publicKeyHex());
        (void)draw(*page);
        check(click(*page, "admins.fiche"), "la fiche de Camille (l'admin+)");
        t = draw(*page);
        check(has(t, "la cl\xC3\xA9 de l'\xC3\xA9" "diteur") && has(t, "L'admin+ a tous les droits") && !shown(*page, "admins.fiche.retirer"), "l'admin+ : sa cle de l'editeur, tous les droits");
        check(click(*page, "admins.fiche.retour"), "retour");
        if (ov) ab::overviewSelect(*ov, leaKey);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le chargement en arriere-plan\n");
    {
        HostRig slow(fx);
        slow.slowOnThread = true;
        auto sp = gui::makeAdminsPage(slow.host());
        const std::string s0 = draw(*sp);
        check(has(s0, "lecture du service"), "le service se lit en arriere-plan : << lecture du service... >>");
        check(has(s0, "Ses actions par jour") && has(s0, kLea), "les chiffres du journal sont deja la");
        check(slow.finishSlow(), "le travail finit");
        const std::string s1 = draw(*sp);
        check(!has(s1, "lecture du service") && has(s1, "1.45.2"), "les chiffres du service arrivent");
        (void)slow.finishSlow(2000);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- un service d'avant la 1.47.0\n");
    {
        sk::Fetchers old;
        const std::string why = license_admin::stats::serviceTooOldText("1.46.0");
        old.series = [why](const license_admin::stats::SeriesQuery&, license_admin::stats::SeriesReply&) { return gui::OpResult::fail(why); };
        old.admins = [why](const license_admin::stats::Period&, license_admin::stats::AdminsReply&) { return gui::OpResult::fail(why); };
        sk::setFetchersForTests(old);
        HostRig orig(fx);
        auto op = gui::makeAdminsPage(orig.host());
        const std::string o = draw(*op);
        check(has(o, "1.47.0") && has(o, "d\xC3\xA9ploie") && shown(*op, "admins.deployer"), "le bandeau : deploie la 1.47.0 ; Deployer le service");
        check(has(o, "Ses actions par jour") && has(o, "Alertes"), "les chiffres du journal restent");
        sk::setFetchersForTests(sk::sampleFetchers([] { return g_now; }));
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- un admin (Lea) : la lecture seule, sa fiche et la sienne seulement\n");
    {
        Fixture lf(source, "admins-vue-lea");
        std::string why;
        auto leaHere = la::createAdminKey(U(lf.admin), kLea, &why);
        check(leaHere != nullptr, "la cle de Lea sur son poste (" + why + ")");
        if (leaHere) {
            adm::AdminList list;
            list.number = 6;
            list.issued = g_now - 3600;
            adm::Admin a;
            a.id = leaHere->keyId();
            a.key = leaHere->publicKeyHex();
            a.name = kLea;
            a.pc = "PC-COMMERCIAL-02";
            a.since = g_now - 57 * 86400;
            a.profile = "Admin LV2";
            a.rights = adm::normalizeRights({"licence_creer", "licence_option", "demandes", "codes"});
            list.admins.push_back(a);
            adm::Admin h;
            h.id = P.hugo->keyId();
            h.key = P.hugo->publicKeyHex();
            h.name = "Hugo Essai";
            h.pc = "PORTABLE-HUGO";
            h.since = g_now - 120 * 86400;
            h.rights = adm::normalizeRights({"licence_creer"});
            list.admins.push_back(h);
            writeFile(lf.admin / "admins.xpgadm", signedList(*fx.ws->signingKey(), list));
            pinEditor(lf.admin, *fx.ws->signingKey());
            lf.ws->reload();
            check(lf.ws->role() == gui::Role::Admin, "Lea : admin");
            HostRig lrig(lf);
            auto lp = gui::makeAdminsPage(lrig.host());
            std::string l = draw(*lp);
            check(has(l, "lecture seule") && shown(*lp, "admins.ma-fiche"), "la liste en lecture seule ; << Ma fiche >>");
            check(!shown(*lp, "admins.vue") && !shown(*lp, "admins.ajouter") && !shown(*lp, "admins.retirer") && !shown(*lp, "admins.onglet.qui"),
                  "ni la vue d'ensemble, ni Ajouter, ni Retirer, ni qui fait quoi");
            check(click(*lp, "admins.ma-fiche"), "Ma fiche");
            l = draw(*lp);
            check(shown(*lp, "admins.fiche.vue") && has(l, "ta fiche") && has(l, "Tes droits : utilis\xC3\xA9s ou non") && has(l, "Quand tu travailles"), "sa fiche : << ta fiche >>, tes droits");
            check(!shown(*lp, "admins.fiche.droits") && !shown(*lp, "admins.fiche.retirer") && !shown(*lp, "admins.fiche.journal"), "ni Ses droits, ni Retirer, ni le journal");
            check(has(l, "Le journal des admins n'est pas sur ce poste"), "le journal : sur le poste de l'admin+ (c'est dit)");
            check(click(*lp, "admins.fiche.retour"), "retour");
            gui::requestPageFocus("admins", "fiche:" + P.hugo->publicKeyHex());
            lp->refresh();
            l = draw(*lp);
            check(!shown(*lp, "admins.fiche.vue") && !has(l, "Quand Hugo travaille"), "la fiche de Hugo : refusee (un admin ne voit que la sienne)");
        }
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- un poste sans journal (vide)\n");
    {
        Fixture ef(source, "admins-vue-vide");
        check(ef.ws->createKey("Camille Essai", true).ok, "un admin+ seul, sans journal");
        HostRig erig(ef);
        auto ep = gui::makeAdminsPage(erig.host());
        const std::string e = draw(*ep);
        check(has(e, "Les alertes viennent du journal des admins") && has(e, "le journal n'est pas relev\xC3\xA9"), "le journal n'est pas releve : c'est dit");
        check(click(*ep, "admins.onglet.qui"), "qui fait quoi");
        const std::string q = draw(*ep);
        check(has(q, "Le journal des admins n'est pas relev\xC3\xA9 sur ce poste"), "qui fait quoi : le journal n'est pas releve");
    }
    return finish();
}
