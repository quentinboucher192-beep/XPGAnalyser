// =============================================================================
//  tests/admin_journal_stats_test.cpp - 1.47.0 : les chiffres du journal des admins [J]
// -----------------------------------------------------------------------------
//  license_admin/JournalStats sur un journal d'essai (fictif) de quatre admins,
//  de mai a novembre 2026 : Camille Essai (l'admin+), Lea Essai (arrivee le
//  14/09, ses droits changes le 02/11 et le 05/11), Hugo Essai (actif jusqu'au
//  05/10, puis quatre actions un dimanche vers minuit, une depuis un autre PC,
//  son Admin pas a jour), Nina Essai (retiree le 28/10 : ses signatures
//  refusees ensuite). Les attendus sont comptes ici a part, a la main (le jour et
//  l'heure de Paris calcules par l'essai lui-meme, pas par le module).
//  Les cas limites : une periode vide, un admin sans action, un admin retire,
//  minuit et le changement d'heure (25/10/2026), une chaine rompue, une ligne
//  du service qui double une ligne de l'admin (autre PC).
// =============================================================================
#include "../src/license_admin/JournalStats.hpp"
#include "../src/license_admin/gui/Workspace.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace js = license_admin::journalstats;
using js::Level;
using js::Seconds;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else {
        std::printf("  ok  %s\n", what.c_str());
    }
}

// ---- l'heure de Paris, calculee ici sans le module (2026 : l'ete du 29/03 01:00 UTC au 25/10 01:00 UTC) ----
std::int64_t civil(int y, int m, int d) {   // jours depuis 1970 (Hinnant)
    y -= m <= 2;
    const int era = y / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}
bool summer2026(int m, int d) { return (m > 3 || (m == 3 && d >= 29)) && (m < 10 || (m == 10 && d < 25)); }
// L'instant UTC d'une heure de Paris (2026 ; pas entre 02:00 et 03:00 les jours de changement).
Seconds paris(int m, int d, int h, int mi) {
    const int off = summer2026(m, d) || (m == 10 && d == 25 && h < 2) ? 7200 : 3600;
    return civil(2026, m, d) * 86400 + h * 3600 + mi * 60 - off;
}
int weekdayOf(int m, int d) { return static_cast<int>(((civil(2026, m, d) % 7) + 7 + 3) % 7); }   // 0 lundi

const std::string kC(64, '1'), kL(64, '2'), kH(64, '3'), kN(64, '4'), kS(64, 'f');

struct Journal {
    std::vector<js::Line> lines;
    std::uint64_t next{1};
    js::Line& add(const std::string& key, const std::string& who, Seconds when, const std::string& action, const std::string& object = {},
                  const std::string& summary = {}, Level level = Level::Info, const std::string& machine = {}, const std::string& pc = {}) {
        js::Line l;
        l.number = next++;
        l.when = when;
        l.received = when + 2;
        l.key = key;
        l.who = who;
        l.action = action;
        l.object = object;
        l.summary = summary.empty() ? "a fait " + action : summary;
        l.level = level;
        l.machine = machine;
        l.pc = pc;
        l.software = "1.47.0";
        lines.push_back(l);
        return lines.back();
    }
};

// Les attendus, comptes a part.
struct Tally {
    int now{0}, before{0};
    std::set<std::int64_t> daysNow;
    std::map<std::string, int> actionNow;
};
// La periode : du 12/10 au 10/11 ; celle d'avant : du 12/09 au 11/10.
bool inNow(int m, int d) { return (m == 10 && d >= 12) || (m == 11 && d <= 10); }
bool inBefore(int m, int d) { return (m == 9 && d >= 12) || (m == 10 && d <= 11); }

const char* kLeaCycle[] = {"licence_creer", "demande_accepter", "courriel", "commande_livrer", "licence_option", "code_revoir"};

struct World {
    Journal j;
    std::vector<js::Admin> admins;
    std::map<std::string, Tally> t;
    std::uint64_t hugoOtherLine{0};
    int leaLicences{0}, leaLicencesNow{0}, leaPeakDay{0};
};

void count(World& w, const std::string& who, int m, int d, const std::string& action) {
    auto& t = w.t[who];
    if (inNow(m, d)) {
        ++t.now;
        t.daysNow.insert(civil(2026, m, d));
        ++t.actionNow[action];
    } else if (inBefore(m, d)) {
        ++t.before;
    }
}

World build() {
    World w;
    auto& j = w.j;
    // ---- les admins (la liste) ----
    js::Admin camille;
    camille.id = "admin+";
    camille.name = "Camille Essai";
    camille.keys = {kC};
    camille.plus = true;
    camille.rights = licensing::admins::allRights();
    js::Admin lea;
    lea.id = "xpg-lea";
    lea.name = "L\xC3\xA9" "a Essai";
    lea.keys = {kL};
    lea.pc = "PC-COMMERCIAL-02";
    lea.machine = "M-LEA";
    lea.since = paris(9, 14, 10, 0);
    lea.rights = licensing::admins::normalizeRights({"licence_creer", "licence_option", "licence_revoquer", "poste_retirer", "annonces", "demandes", "codes",
                                                     "client_supprimer", "clients_exporter", "courriels"});
    js::Admin hugo;
    hugo.id = "xpg-hugo";
    hugo.name = "Hugo Essai";
    hugo.keys = {kH};
    hugo.pc = "PC-HUGO-01";
    hugo.machine = "M-HUGO";
    hugo.since = paris(6, 1, 9, 0);
    hugo.rights = licensing::admins::normalizeRights({"licence_creer", "demandes", "courriels", "codes"});
    js::Admin nina;
    nina.id = "xpg-nina";
    nina.name = "Nina Essai";
    nina.keys = {kN};
    nina.machine = "M-NINA";
    nina.since = paris(5, 1, 9, 0);
    nina.retired = paris(10, 28, 9, 0);
    nina.rights = {"licence_creer", "courriels"};
    w.admins = {camille, lea, hugo, nina};

    // ---- Nina : un courriel chaque jour ouvre a 10:00, du 01/09 au 27/10 ; retiree le 28/10 ----
    for (int m = 9; m <= 10; ++m)
        for (int d = 1; d <= (m == 9 ? 30 : 27); ++d)
            if (weekdayOf(m, d) < 5) {
                j.add(kN, "Nina Essai", paris(m, d, 10, 0), "courriel", "client@exemple.fr", "a \xC3\xA9" "crit \xC3\xA0 un client", Level::Info, "M-NINA");
                count(w, "nina", m, d, "courriel");
            }
    // ---- Hugo : deux actions par jour ouvre (08:30, 17:00) du 03/08 au 05/10 ----
    for (int m = 8; m <= 10; ++m)
        for (int d = (m == 8 ? 3 : 1); d <= (m == 10 ? 5 : (m == 8 ? 31 : 30)); ++d)
            if (weekdayOf(m, d) < 5) {
                j.add(kH, "Hugo Essai", paris(m, d, 8, 30), "licence_creer", "XPG-H-" + std::to_string(m * 100 + d), {}, Level::Medium, "M-HUGO");
                count(w, "hugo", m, d, "licence_creer");
                j.add(kH, "Hugo Essai", paris(m, d, 17, 0), "courriel", "c@exemple.fr", {}, Level::Info, "M-HUGO");
                count(w, "hugo", m, d, "courriel");
            }
    // ---- Camille : Lea ajoutee (14/09), la boutique et Stripe (02/10) ----
    j.add(kC, "admin+", paris(9, 14, 10, 0), "admin_ajouter", "xpg-lea", "a ajout\xC3\xA9 l'admin L\xC3\xA9" "a Essai", Level::Critical).after =
        "{\"droits\":[\"licence_creer\",\"licence_prolonger\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\","
        "\"client_supprimer\",\"courriels\"]}";
    count(w, "camille", 9, 14, "admin_ajouter");
    j.add(kC, "admin+", paris(10, 2, 15, 0), "stripe_brancher", "stripe", "a branch\xC3\xA9 Stripe", Level::Critical);
    count(w, "camille", 10, 2, "stripe_brancher");
    // ---- Lea : trois actions par jour ouvre (09:15, 11:30, 16:45) du 15/09 au 10/11 (sauf les 19 et 20/10) ----
    int k = 0;
    for (int m = 9; m <= 11; ++m)
        for (int d = (m == 9 ? 15 : 1); d <= (m == 11 ? 10 : (m == 9 ? 30 : 31)); ++d) {
            if (weekdayOf(m, d) >= 5 || (m == 10 && (d == 19 || d == 20))) continue;
            for (const auto& [h, mi] : std::vector<std::pair<int, int>>{{9, 15}, {11, 30}, {16, 45}}) {
                if (m == 11 && d == 10 && h == 16) continue;   // pas encore : il est midi
                const std::string a = kLeaCycle[k++ % 6];
                std::string obj = a == "licence_creer" ? "XPG-2026-" + std::to_string(10000 + ++w.leaLicences) : "objet";
                if (a == "licence_creer" && inNow(m, d)) ++w.leaLicencesNow;
                j.add(kL, "L\xC3\xA9" "a Essai", paris(m, d, h, mi), a, obj, {}, Level::Info, "M-LEA", "PC-COMMERCIAL-02");
                count(w, "lea", m, d, a);
            }
        }
    // Un pic : le vendredi 30/10, 16 licences de plus (10:00 a 13:45).
    for (int i = 0; i < 16; ++i) {
        j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 30, 10 + i / 4, (i % 4) * 15), "licence_creer", "XPG-2026-" + std::to_string(20000 + i), {}, Level::Medium, "M-LEA");
        count(w, "lea", 10, 30, "licence_creer");
        ++w.leaLicencesNow;
    }
    // Ce qui compte : deux clients supprimes, deux revocations (critiques), un export (apres le 05/11).
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 22, 10, 0), "client_supprimer", "Client d'essai 14", {}, Level::Medium, "M-LEA");
    count(w, "lea", 10, 22, "client_supprimer");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(11, 3, 10, 30), "client_supprimer", "Studio d'essai 2", {}, Level::Medium, "M-LEA");
    count(w, "lea", 11, 3, "client_supprimer");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 15, 14, 0), "licence_revoquer", "XPG-2026-00359", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2026-00359", Level::Critical, "M-LEA");
    count(w, "lea", 10, 15, "licence_revoquer");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(11, 6, 15, 0), "licence_revoquer", "XPG-2025-00342", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2025-00342", Level::Critical, "M-LEA");
    count(w, "lea", 11, 6, "licence_revoquer");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(11, 6, 11, 0), "clients_exporter", "CSV", {}, Level::Medium, "M-LEA");
    count(w, "lea", 11, 6, "clients_exporter");
    // Les heures inhabituelles : mercredi 21/10 21:03, dimanche 25/10 (le changement d'heure) 22:14 deux fois, et minuit
    // passe : 31/10 23:30 UTC = dimanche 01/11 00:30 a Paris.
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 21, 21, 3), "courriel", "client@exemple.fr", {}, Level::Info, "M-LEA");
    count(w, "lea", 10, 21, "courriel");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 25, 22, 14), "demande_accepter", "demande 51", {}, Level::Info, "M-LEA");
    count(w, "lea", 10, 25, "demande_accepter");
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 25, 22, 14), "code_revoir", "XPG-2026-00398", {}, Level::Medium, "M-LEA");
    count(w, "lea", 10, 25, "code_revoir");
    j.add(kL, "L\xC3\xA9" "a Essai", civil(2026, 10, 31) * 86400 + 23 * 3600 + 30 * 60, "courriel", "minuit@exemple.fr", {}, Level::Info, "M-LEA");
    count(w, "lea", 11, 1, "courriel");
    // Depuis un autre PC : samedi 24/10 19:05.
    j.add(kL, "L\xC3\xA9" "a Essai", paris(10, 24, 19, 5), "licence_creer", "XPG-2026-30000", {}, Level::Medium, "M-MAISON", "PC-MAISON-LEA");
    count(w, "lea", 10, 24, "licence_creer");
    ++w.leaLicencesNow;
    // Une signature refusee pour Lea : un droit manquant (prolonger, retire le 02/11).
    j.add(kS, "le service", paris(11, 4, 10, 2), "signature_refusee", "xpg-lea", "a refus\xC3\xA9 un appel sign\xC3\xA9 par L\xC3\xA9" "a Essai", Level::Critical).detail =
        "{\"cle\":\"" + kL + "\",\"nom\":\"L\xC3\xA9" "a Essai\",\"route\":\"PUT /admin/licences\",\"raison\":\"droit\",\"droit\":\"licence_prolonger\"}";
    // ---- Camille : les droits de Lea changes, Nina retiree, une version, la criticite, une revocation, les sauvegardes ----
    auto& d1 = j.add(kC, "admin+", paris(11, 2, 10, 0), "admin_droits", "xpg-lea", "a chang\xC3\xA9 les droits de L\xC3\xA9" "a Essai", Level::Critical);
    d1.before = "{\"droits\":[\"licence_creer\",\"licence_prolonger\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\","
                "\"client_supprimer\",\"courriels\"]}";
    d1.after = "{\"droits\":[\"licence_creer\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\",\"client_supprimer\","
               "\"courriels\"]}";
    d1.detail = "{\"nom\":\"L\xC3\xA9" "a Essai\",\"profil\":\"Admin LV2\"}";
    count(w, "camille", 11, 2, "admin_droits");
    auto& d2 = j.add(kC, "admin+", paris(11, 5, 9, 0), "admin_droits", "xpg-lea", "a chang\xC3\xA9 les droits de L\xC3\xA9" "a Essai", Level::Critical);
    d2.before = d1.after;
    d2.after = "{\"droits\":[\"licence_creer\",\"licence_option\",\"licence_revoquer\",\"poste_retirer\",\"annonces\",\"demandes\",\"codes\",\"client_supprimer\","
               "\"clients_exporter\",\"courriels\"]}";
    count(w, "camille", 11, 5, "admin_droits");
    j.add(kC, "admin+", paris(10, 28, 9, 0), "admin_retirer", "xpg-nina", "a retir\xC3\xA9 l'admin Nina Essai", Level::Critical).before = "{\"droits\":[\"licence_creer\",\"courriels\"]}";
    count(w, "camille", 10, 28, "admin_retirer");
    j.add(kC, "admin+", paris(11, 4, 11, 0), "version_publier", "1.47.0", "a publi\xC3\xA9 la version 1.47.0", Level::High);
    count(w, "camille", 11, 4, "version_publier");
    j.add(kC, "admin+", paris(10, 20, 15, 0), "criticite", "client_supprimer", "a chang\xC3\xA9 la criticit\xC3\xA9 de Supprimer un client", Level::Medium);
    count(w, "camille", 10, 20, "criticite");
    j.add(kC, "admin+", paris(11, 10, 10, 48), "licence_revoquer", "XPG-2026-00377", "a r\xC3\xA9voqu\xC3\xA9 la licence XPG-2026-00377", Level::Critical);
    count(w, "camille", 11, 10, "licence_revoquer");
    for (int m = 9; m <= 11; ++m)
        for (int d = 1; d <= (m == 11 ? 10 : (m == 9 ? 30 : 31)); ++d) {
            j.add(kS, "le service", paris(m, d, 3, 0), "sauvegarde", "", "a export\xC3\xA9 la sauvegarde compl\xC3\xA8te du service", Level::Info);
        }
    // ---- Hugo : un apres-midi depuis un autre PC (03/11) - la ligne du service la double ; puis un dimanche vers minuit ----
    auto& other = j.add(kH, "Hugo Essai", paris(11, 3, 14, 0), "licence_creer", "XPG-H-1103", {}, Level::Medium, "M-MAISON-07", "PC-MAISON-07");
    w.hugoOtherLine = other.number;
    count(w, "hugo", 11, 3, "licence_creer");
    j.add(kS, "le service", paris(11, 3, 14, 0), "autre_pc", "xpg-hugo", "une entr\xC3\xA9" "e de Hugo Essai vient d'un autre PC que le sien", Level::High).detail =
        "{\"ligne\":" + std::to_string(w.hugoOtherLine) + ",\"cle\":\"" + kH + "\",\"poste_liste\":\"M-HUGO\",\"poste_entree\":\"M-MAISON-07\",\"pc\":\"PC-MAISON-07\"}";
    for (const int mi : {40, 46, 52, 58}) {
        j.add(kH, "Hugo Essai", paris(11, 8, 23, mi), "courriel", "nuit@exemple.fr", {}, Level::Info, "M-HUGO");
        count(w, "hugo", 11, 8, "courriel");
    }
    // ---- Nina, retiree : trois signatures refusees (la cle retiree) ----
    for (const auto& [m, d, h, mi] : std::vector<std::array<int, 4>>{{11, 1, 10, 0}, {11, 5, 10, 0}, {11, 10, 8, 12}})
        j.add(kS, "le service", paris(m, d, h, mi), "signature_refusee", "xpg-nina", "a refus\xC3\xA9 un appel sign\xC3\xA9 par Nina Essai (admin retir\xC3\xA9)",
              Level::Critical)
            .detail = "{\"cle\":\"" + kN + "\",\"nom\":\"Nina Essai\",\"route\":\"GET /admin/etat\",\"retire\":" + std::to_string(paris(10, 28, 9, 0)) + "}";
    // Le journal dans l'ordre de reception (les plus anciennes d'abord), numerote de nouveau comme le service.
    std::stable_sort(j.lines.begin(), j.lines.end(), [](const js::Line& a, const js::Line& b) { return a.at() < b.at(); });
    std::map<std::uint64_t, std::uint64_t> renumber;
    for (std::size_t i = 0; i < j.lines.size(); ++i) {
        renumber[j.lines[i].number] = i + 1;
        j.lines[i].number = i + 1;
    }
    w.hugoOtherLine = renumber[w.hugoOtherLine];
    for (auto& l : j.lines)
        if (l.action == "autre_pc") l.detail = "{\"ligne\":" + std::to_string(w.hugoOtherLine) + ",\"cle\":\"" + kH +
                                               "\",\"poste_liste\":\"M-HUGO\",\"poste_entree\":\"M-MAISON-07\",\"pc\":\"PC-MAISON-07\"}";
    return w;
}

const js::AdminStats* find(const js::Overview& v, const std::string& id) {
    for (const auto& s : v.admins)
        if (s.id == id) return &s;
    return nullptr;
}
int sum(const std::vector<int>& v) {
    int s = 0;
    for (const int x : v) s += x;
    return s;
}
bool has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

} // namespace

int main() {
    std::printf("-- 1.47.0 : les chiffres du journal des admins (license_admin/JournalStats)\n");
    const js::TimeZone tz = js::TimeZone::paris();
    const Seconds now = paris(11, 10, 12, 0);

    // ---- l'heure de Paris ----
    check(js::parisOffset(civil(2026, 3, 29) * 86400 + 3600 - 1) == 3600 && js::parisOffset(civil(2026, 3, 29) * 86400 + 3600) == 7200,
          "le passage a l'heure d'ete : le 29/03/2026 a 01:00 UTC");
    check(js::parisOffset(civil(2026, 10, 25) * 86400 + 3600 - 1) == 7200 && js::parisOffset(civil(2026, 10, 25) * 86400 + 3600) == 3600,
          "le retour a l'heure d'hiver : le 25/10/2026 a 01:00 UTC");
    const Seconds midnight = civil(2026, 10, 31) * 86400 + 23 * 3600 + 30 * 60;   // 31/10 23:30 UTC
    check(js::dayText(js::localDay(midnight, tz)) == "2026-11-01" && js::localHour(midnight, tz) == 0 && js::weekday(js::localDay(midnight, tz)) == 6,
          "minuit passe : 31/10 23:30 UTC = dimanche 01/11 00:30 a Paris");
    check(js::localHour(civil(2026, 10, 24) * 86400 + 22 * 3600 + 30 * 60, tz) == 0 &&
              js::dayText(js::localDay(civil(2026, 10, 24) * 86400 + 22 * 3600 + 30 * 60, tz)) == "2026-10-25",
          "l'ete : 24/10 22:30 UTC = 25/10 00:30 a Paris");
    std::int64_t d10 = 0;
    check(js::parseDay("2026-11-10", d10) && js::dayStart(d10, tz) == civil(2026, 11, 9) * 86400 + 23 * 3600 &&
              js::dayStart(civil(2026, 7, 1), tz) == civil(2026, 6, 30) * 86400 + 22 * 3600,
          "le minuit de Paris : 10/11 = 09/11 23:00 UTC (hiver), 01/07 = 30/06 22:00 UTC (ete)");
    const js::Period p30 = js::lastDays(now, 30, tz);
    check(p30.from() == "2026-10-12" && p30.to() == "2026-11-10" && p30.days() == 30, "30 jours : du 12/10 au 10/11 (aujourd'hui compris)");
    const js::Period prev = js::previousOf(p30);
    check(prev.from() == "2026-09-12" && prev.to() == "2026-10-11", "la periode d'avant : du 12/09 au 11/10");
    const js::Period m12 = js::lastMonths(now, 12, tz);
    check(m12.from() == "2025-11-11" && m12.to() == "2026-11-10", "12 mois : du 11/11/2025 au 10/11/2026");
    js::Period bad;
    check(!js::between("2026-02-30", "2026-03-01", bad) && !js::between("2026-11-10", "2026-11-01", bad) && js::between("2026-11-01", "2026-11-10", bad) &&
              bad.days() == 10,
          "deux dates : le 30 fevrier et l'envers refuses ; du 01/11 au 10/11 = 10 jours");
    check(js::weekday(civil(2026, 11, 8)) == 6 && js::weekday(civil(2026, 11, 9)) == 0, "le 08/11/2026 est un dimanche, le 09/11 un lundi");

    // ---- les 14 types : les 13 droits, puis la boutique ----
    const auto& types = js::types();
    bool order = true;
    for (std::size_t i = 0; i < 13; ++i) order = order && std::string(types[i].id) == licensing::admins::rights()[i].id;
    check(order && std::string(types[13].label) == "Boutique : livrer, revoir" && std::string(types[0].label) == "Cr\xC3\xA9" "er et signer une licence",
          "les types : les 13 droits dans l'ordre de la page Droits, puis la boutique");
    check(js::typeOf("demande_refuser") == 8 && js::typeOf("code_revoir") == 9 && js::typeOf("client_restaurer") == 10 && js::typeOf("commande_revoir") == 13 &&
              js::typeOf("connexion") == -1 && js::typeOf("version_publier") == -1 && js::reserved("version_publier") && !js::reserved("licence_creer"),
          "les actions du journal rangees dans leur type ; les reservees a part");

    // ---- le journal d'essai ----
    World w = build();
    js::Inputs in;
    in.lines = w.j.lines;
    in.admins = w.admins;
    in.now = now;
    in.period = p30;
    in.latestAdminVersion = "1.47.0";
    js::ServiceFigures hugoSvc;
    hugoSvc.adminVersion = "1.45.2";
    hugoSvc.lastSeen = paris(11, 9, 18, 20);
    in.service["xpg-hugo"] = hugoSvc;
    js::ServiceFigures leaSvc;
    leaSvc.licences = 41;
    leaSvc.licencesBefore = 34;
    leaSvc.requests = 34;
    leaSvc.meanDelay = 4 * 3600 + 5 * 60;
    leaSvc.amount = 487000;
    leaSvc.adminVersion = "1.47.0";
    in.service["xpg-lea"] = leaSvc;
    const js::Overview v = js::overview(in);
    std::printf("   (%zu lignes au journal d'essai)\n", in.lines.size());

    const auto* lea = find(v, "xpg-lea");
    const auto* hugo = find(v, "xpg-hugo");
    const auto* nina = find(v, "xpg-nina");
    const auto* cam = find(v, "admin+");
    check(lea && hugo && nina && cam && v.admins.size() == 4 && v.admins[0].plus && v.admins[1].name == "L\xC3\xA9" "a Essai",
          "les quatre admins, dans l'ordre donne (Camille, Lea, Hugo, Nina)");
    if (!lea || !hugo || !nina || !cam) return 1;

    // ---- Lea ----
    const auto& tl = w.t["lea"];
    check(lea->actions == tl.now && lea->actionsBefore == tl.before,
          "L\xC3\xA9" "a : " + std::to_string(lea->actions) + " actions (attendu " + std::to_string(tl.now) + "), avant " + std::to_string(lea->actionsBefore) + " (attendu " +
              std::to_string(tl.before) + ")");
    int typedSum = 0;
    for (const int x : lea->byType) typedSum += x;
    int levels = 0;
    for (const int x : lea->byLevel) levels += x;
    check(lea->byType[0] == tl.actionNow.at("licence_creer") && lea->byType[8] == tl.actionNow.at("demande_accepter") && lea->byType[js::kShop] == tl.actionNow.at("commande_livrer") &&
              typedSum == lea->typed && lea->typed == lea->actions && levels == lea->actions,
          "L\xC3\xA9" "a : par type (" + std::to_string(lea->byType[0]) + " licences, " + std::to_string(lea->byType[js::kShop]) + " boutique) et par criticite, les sommes justes");
    check(lea->byType[4] == 0 && lea->byType[7] == 0 && lea->byType[1] == 0, "L\xC3\xA9" "a : Retirer un PC, Annonces, Prolonger : 0");
    check(lea->perDay.size() == 30 && sum(lea->perDay) == lea->actions && lea->perDay[static_cast<std::size_t>(civil(2026, 10, 30) - civil(2026, 10, 12))] == 19 &&
              lea->perDay[static_cast<std::size_t>(civil(2026, 10, 19) - civil(2026, 10, 12))] == 0,
          "L\xC3\xA9" "a : par jour (30 points) - 19 le 30/10, absente le 19/10");
    check(lea->activeDays == static_cast<int>(tl.daysNow.size()), "L\xC3\xA9" "a : " + std::to_string(lea->activeDays) + " jours actifs (attendu " + std::to_string(tl.daysNow.size()) + ")");
    check(lea->streak == 15 && lea->bestStreak == 24,
          "L\xC3\xA9" "a : s\xC3\xA9rie en cours 15 jours ouvr\xC3\xA9s (depuis le 21/10), record 24 (" + std::to_string(lea->streak) + ", " + std::to_string(lea->bestStreak) + ")");
    check(lea->peaks.size() == 1 && js::dayText(lea->peaks[0].day) == "2026-10-30" && lea->peaks[0].count == 19 && lea->peaks[0].factor > 3.0,
          "L\xC3\xA9" "a : un pic, le 30/10 (19 actions, plus de 3 fois sa moyenne)");
    check(lea->habitsKnown && lea->usualStart == 9 * 60 + 15 && lea->usualEnd == 16 * 60 + 45,
          "L\xC3\xA9" "a : ses heures habituelles 09:15 -> 16:45 (" + std::to_string(lea->usualStart) + " -> " + std::to_string(lea->usualEnd) + " min)");
    check(lea->unusual.size() == 5 && lea->dayHourUnusual[6][0] == 1 && lea->dayHourUnusual[6][22] == 2 && lea->dayHourUnusual[2][21] == 1 &&
              lea->dayHourUnusual[5][19] == 1,
          "L\xC3\xA9" "a : 5 actions a une heure inhabituelle (mer. 21:03, dim. 25/10 22:14 x2, sam. 19:05, dim. 01/11 00:30)");
    check(lea->dayHour[6][0] == 1 && lea->dayHour[0][9] > 0, "la carte jour x heure : dimanche 0 h (minuit passe), lundi 9 h");
    check(lea->unused.size() == 2 && has(lea->unused, "poste_retirer") && has(lea->unused, "annonces"),
          "L\xC3\xA9" "a : deux droits jamais utilis\xC3\xA9s (Retirer un PC, Annonces) ; Exporter, donn\xC3\xA9 le 05/11, pas encore compt\xC3\xA9");
    check(lea->rights.size() == 13 && lea->rights[11].held && lea->rights[11].given == paris(11, 5, 9, 0) && !lea->rights[1].held && lea->rights[5].held == false,
          "L\xC3\xA9" "a : ses 13 droits (Exporter donn\xC3\xA9 le 05/11 ; Prolonger et Offres non)");
    check(lea->rightsChanges.size() == 3 && lea->rightsChanges[0].joined && lea->rightsChanges[1].by == "Camille Essai" && lea->rightsChanges[1].removed == std::vector<std::string>{"licence_prolonger"} &&
              lea->rightsChanges[1].profile == "Admin LV2" && lea->rightsChanges[2].added == std::vector<std::string>{"clients_exporter"},
          "L\xC3\xA9" "a : ses droits chang\xC3\xA9s (ajout\xC3\xA9" "e le 14/09 ; -Prolonger le 02/11 ; +Exporter le 05/11, par Camille Essai)");
    check(lea->refused.size() == 1 && lea->refused[0].reason == js::RefusalReason::MissingRight && lea->refused[0].right == "licence_prolonger",
          "L\xC3\xA9" "a : une signature refus\xC3\xA9" "e, droit manquant (Prolonger)");
    check(lea->otherPc.size() == 1 && lea->otherPc[0].pc == "PC-MAISON-LEA" && lea->otherPc[0].usualPc == "PC-COMMERCIAL-02", "L\xC3\xA9" "a : une action depuis un autre PC (PC-MAISON-LEA)");
    check(lea->deletions.size() == 2 && lea->revocations.size() == 2 && static_cast<int>(lea->codeReviews.size()) == tl.actionNow.at("code_revoir"),
          "L\xC3\xA9" "a : le d\xC3\xA9tail - 2 clients supprim\xC3\xA9s, 2 r\xC3\xA9vocations, les codes revus");
    check(lea->chain.ok() && lea->chain.lines > 0 && lea->last.when == paris(11, 10, 11, 30), "L\xC3\xA9" "a : la cha\xC3\xAEne v\xC3\xA9rifi\xC3\xA9" "e pour ses lignes ; sa derni\xC3\xA8re action (10/11 11:30)");
    check(!lea->licencesFromJournal && lea->licences == 41 && lea->requests == 34, "L\xC3\xA9" "a : licences et demandes : les chiffres du service quand il les donne");
    {
        const auto k30 = static_cast<std::size_t>(civil(2026, 10, 30) - civil(2026, 10, 12));
        const auto k19 = static_cast<std::size_t>(civil(2026, 10, 19) - civil(2026, 10, 12));
        const auto k01 = static_cast<std::size_t>(civil(2026, 11, 1) - civil(2026, 10, 12));
        check(lea->dayFirst.size() == 30 && lea->dayFirst[k30] == 9 * 60 + 15 && lea->dayLast[k30] == 16 * 60 + 45 && lea->dayFirst[k19] == -1 &&
                  lea->dayFirst[k01] == 30 && lea->dayLast[k01] == 30,
              "L\xC3\xA9" "a : le d\xC3\xA9" "but et la fin de ses journ\xC3\xA9" "es (30/10 : 09:15 -> 16:45 ; 01/11 : 00:30 ; absente le 19/10)");
        check(lea->recent.size() == 5 && lea->recent[0].when == paris(11, 10, 11, 30) && lea->recent[0].when >= lea->recent[1].when &&
                  lea->recent[1].when >= lea->recent[4].when,
              "L\xC3\xA9" "a : ses 5 derni\xC3\xA8res actions, la plus r\xC3\xA9" "cente d'abord");
        check(lea->chain.first == paris(9, 15, 9, 15), "L\xC3\xA9" "a : sa premi\xC3\xA8re ligne (15/09 09:15)");
    }

    // ---- Hugo ----
    const auto& th = w.t["hugo"];
    check(hugo->actions == th.now && hugo->actionsBefore == th.before && hugo->actions == 5, "Hugo : 5 actions sur la p\xC3\xA9riode, " + std::to_string(hugo->actionsBefore) + " avant");
    check(hugo->unusual.size() == 4 && hugo->dayHourUnusual[6][23] == 4, "Hugo : 4 actions a une heure inhabituelle (dimanche 08/11, 23 h)");
    check(hugo->otherPc.size() == 1 && hugo->otherPc[0].line.number == w.hugoOtherLine, "Hugo : l'autre PC une seule fois (sa ligne et celle du service ne comptent qu'une)");
    check(hugo->unused.size() == 2 && has(hugo->unused, "demandes") && has(hugo->unused, "codes"), "Hugo : Demandes et Revoir le code jamais utilis\xC3\xA9s");
    check(hugo->licencesFromJournal && hugo->licences == 1, "Hugo : sans le service, ses licences sign\xC3\xA9" "es lues dans le journal (1)");
    check(hugo->peaks.empty() && hugo->streak == 0, "Hugo : pas de pic, pas de s\xC3\xA9rie (dimanche seulement)");

    // ---- Nina (retiree) ----
    check(nina->retired && nina->actions == w.t["nina"].now && nina->refused.size() == 3 && nina->refused[2].reason == js::RefusalReason::RemovedKey && nina->unused.empty(),
          "Nina (retir\xC3\xA9" "e) : ses actions jusqu'au 27/10, 3 signatures refus\xC3\xA9" "es (cl\xC3\xA9 retir\xC3\xA9" "e), pas de droits << jamais utilis\xC3\xA9s >>");

    // ---- Camille, l'admin+ : ce que lui seul fait ----
    check(v.plus.versions.size() == 1 && v.plus.backups.size() == 30 && v.plus.adminsRemoved.size() == 1 && v.plus.adminsAdded.empty() && v.plus.rights.size() == 2 &&
              v.plus.criticities.size() == 1 && v.plus.stripe.empty() && v.plus.lastBackup.when == paris(11, 10, 3, 0),
          "l'admin+ : 1 version, 30 sauvegardes, 1 admin retir\xC3\xA9, 2 droits chang\xC3\xA9s, 1 criticit\xC3\xA9 ; la derni\xC3\xA8re sauvegarde ce matin");
    check(cam->actions == w.t["camille"].now && cam->typed == 1 && cam->habitsKnown == false && cam->activeDays == 6,
          "l'admin+ : ses gestes (r\xC3\xA9serv\xC3\xA9s compris, pas les sauvegardes de la nuit) ; un seul des 14 types (la r\xC3\xA9vocation)");

    // ---- la vue d'ensemble ----
    check(v.activeAdmins == 2 && v.retiredAdmins == 1, "en t\xC3\xAAte : 2 admins actifs, 1 retir\xC3\xA9 (1.51.0 : plus de demandes en attente)");
    check(v.actions == lea->actions + hugo->actions + nina->actions + cam->actions && v.typed == lea->typed + hugo->typed + nina->typed + cam->typed,
          "les actions de tous (" + std::to_string(v.actions) + ") ; toutes hors r\xC3\xA9serv\xC3\xA9" "es (" + std::to_string(v.typed) + ")");
    check(v.ranking.size() == 4 && v.ranking[0].id == "xpg-lea" && v.ranking[0].licences == 41 && v.ranking[0].amount == 487000 && std::isnan(v.ranking[1].amount),
          "le classement : L\xC3\xA9" "a en t\xC3\xAAte (les chiffres du service : 41 licences, 4 870 \xE2\x82\xAC) ; inconnu sans le service");
    check(v.shares.size() == 15 && v.shares[0].type == -1 && v.shares[0].total == v.typed && v.shares[1].label == "Cr\xC3\xA9" "er et signer une licence",
          "qui fait quoi : << Toutes (hors r\xC3\xA9serv\xC3\xA9" "es) >> puis les 14 types");
    check(v.shares[1].leader == 1 && v.shares[1].changeInPercent, "qui fait quoi : L\xC3\xA9" "a en t\xC3\xAAte des licences sign\xC3\xA9" "es ; l'\xC3\xA9" "cart en %");
    check(v.shares[1 + 9].alone && v.shares[1 + 9].leader == 1 && v.shares[1 + 4].total == 0 && v.shares[1 + 4].leader == -1,
          "qui fait quoi : Revoir le code, L\xC3\xA9" "a seule ; Retirer un PC, personne");
    check(v.shares[1 + 3].perAdmin[0] == 1 && v.shares[1 + 3].perAdmin[1] == 2 && v.shares[1 + 3].leader == 1 && !v.shares[1 + 3].alone &&
              !v.shares[1 + 3].changeInPercent && v.shares[1 + 3].totalBefore == 0 && v.shares[1 + 3].changeCount == 3 && std::isnan(v.shares[1 + 3].changePct),
          "qui fait quoi : R\xC3\xA9voquer une licence, Camille 1 et L\xC3\xA9" "a 2 ; l'\xC3\xA9" "cart en nombre (rien avant : +3)");
    check(v.chain.ok() && v.chain.lines == static_cast<int>(in.lines.size()), "la cha\xC3\xAEne de tout le journal v\xC3\xA9rifi\xC3\xA9" "e");

    // ---- les alertes, chacune avec son geste ----
    std::map<js::AlertKind, std::vector<const js::Alert*>> byKind;
    for (const auto& a : v.alerts) byKind[a.kind].push_back(&a);
    check(!v.alerts.empty() && v.alerts[0].severity == js::Severity::Critical && v.alerts[0].adminId == "xpg-nina" &&
              v.alerts[0].text.find("3 signatures refus\xC3\xA9" "es") != std::string::npos && v.alerts[0].gesture == "Voir dans le journal",
          "alertes : d'abord les critiques - Nina, 3 signatures refus\xC3\xA9" "es (" + (v.alerts.empty() ? std::string() : v.alerts[0].text) + ")");
    check(byKind[js::AlertKind::RefusedSignatures].size() == 2 && byKind[js::AlertKind::RefusedSignatures][1]->text.find("droit manquant") != std::string::npos,
          "alertes : L\xC3\xA9" "a, une signature refus\xC3\xA9" "e (droit manquant)");
    check(byKind[js::AlertKind::AdminOutdated].size() == 1 && byKind[js::AlertKind::AdminOutdated][0]->adminId == "xpg-hugo" &&
              byKind[js::AlertKind::AdminOutdated][0]->gesture == "Lui envoyer la mise \xC3\xA0 jour" && byKind[js::AlertKind::AdminOutdated][0]->text.find("1.45.2") != std::string::npos,
          "alertes : Hugo, son Admin en 1.45.2 (Lui envoyer la mise \xC3\xA0 jour)");
    const js::Alert* hugoUnusual = nullptr;
    for (const auto* a : byKind[js::AlertKind::UnusualHour])
        if (a->adminId == "xpg-hugo") hugoUnusual = a;
    check(hugoUnusual && hugoUnusual->text.find("dim. 08/11 de 23:40 \xC3\xA0 23:58") != std::string::npos && hugoUnusual->gesture == "Voir ces 4 actions" &&
              hugoUnusual->lines.size() == 4 && hugoUnusual->severity == js::Severity::High,
          "alertes : Hugo, 4 actions a une heure inhabituelle, dim. 08/11 de 23:40 a 23:58 (" + (hugoUnusual ? hugoUnusual->text : std::string("absente")) + ")");
    check(byKind[js::AlertKind::OtherPc].size() == 2 && byKind[js::AlertKind::RightsUnused].size() == 2 && byKind[js::AlertKind::RightsUnused][0]->gesture == "Retirer ces droits ?",
          "alertes : un autre PC (L\xC3\xA9" "a, Hugo), des droits jamais utilis\xC3\xA9s (Retirer ces droits ?)");
    check(byKind[js::AlertKind::RecentCritical].size() == 2 && byKind[js::AlertKind::RecentCritical][0]->adminId == "admin+" &&
              byKind[js::AlertKind::RecentCritical][0]->text.find("XPG-2026-00377") != std::string::npos && byKind[js::AlertKind::RecentCritical][0]->gesture == "Voir la ligne",
          "alertes : les actions critiques des 7 derniers jours (Camille ce matin, L\xC3\xA9" "a le 06/11), les plus r\xC3\xA9" "centes d'abord");
    bool accepting = false;   // 1.51.0 : plus de demande d'admin, plus d'alerte << Verifier et accepter... >>
    for (const auto& a : v.alerts) accepting = accepting || a.gesture.find("accepter") != std::string::npos || a.gestureId == "accepter";
    check(!accepting && byKind[js::AlertKind::Inactive].empty(), "alertes : aucune demande a accepter (1.51.0) ; personne d'inactif");
    bool sorted = true;
    for (std::size_t i = 1; i < v.alerts.size(); ++i) sorted = sorted && static_cast<int>(v.alerts[i - 1].severity) <= static_cast<int>(v.alerts[i].severity);
    check(sorted, "alertes : Critique, puis Haute, puis \xC3\x80 voir");

    // ---- la fiche seule (adminStats) : les memes chiffres ----
    const js::AdminStats one = js::adminStats(in, "xpg-lea");
    check(one.actions == lea->actions && one.unused == lea->unused && js::adminStats(in, "xpg-inconnu").id.empty(), "la fiche seule : les m\xC3\xAAmes chiffres ; un id inconnu : vide");

    // ---- une periode vide (janvier : rien au journal) ----
    {
        js::Inputs e = in;
        js::between("2026-01-01", "2026-01-31", e.period);
        const js::Overview ev = js::overview(e);
        bool zero = ev.actions == 0 && std::isnan(ev.change);
        for (const auto& s : ev.admins) zero = zero && s.actions == 0 && s.perDay.size() == 31 && sum(s.perDay) == 0 && s.peaks.empty() && s.unusual.empty();
        check(zero, "une p\xC3\xA9riode vide (janvier) : 0 partout, 31 jours, pas d'\xC3\xA9" "cart (rien avant)");
    }
    // ---- un admin sans action, un admin inactif depuis 30 jours ----
    {
        js::Inputs e;
        e.now = now;
        e.period = p30;
        e.admins = {w.admins[0], w.admins[1], w.admins[2]};
        for (const auto& l : w.j.lines)
            if (l.key == kH && l.at() < paris(10, 6, 0, 0)) e.lines.push_back(l);
        const js::Overview ev = js::overview(e);
        const auto* l2 = find(ev, "xpg-lea");
        const auto* h2 = find(ev, "xpg-hugo");
        std::vector<std::string> texts;
        for (const auto& a : ev.alerts)
            if (a.kind == js::AlertKind::Inactive) texts.push_back(a.text);
        check(l2 && l2->actions == 0 && l2->last.number == 0 && !l2->habitsKnown && l2->streak == 0 && l2->unused.size() == 10,
              "un admin sans aucune action : rien, pas d'habitudes, ses 10 droits jamais utilis\xC3\xA9s");
        check(h2 && h2->actions == 0 && texts.size() == 2 && texts[0].find("aucune action depuis") != std::string::npos,
              "inactif depuis 30 jours : Hugo (le 05/10) et L\xC3\xA9" "a (depuis son arriv\xC3\xA9" "e) - " + (texts.empty() ? std::string() : texts[0]));
    }
    // ---- une chaine rompue (une ligne modifiee) ----
    {
        js::Inputs e = in;
        for (auto& l : e.lines)
            if (l.key == kH && l.action == "courriel" && l.at() == paris(9, 30, 17, 0)) {
                l.verified = false;
                l.chainOk = false;
                for (auto& x : e.lines)
                    if (x.number > l.number) x.chainOk = false;
                break;
            }
        const js::Overview ev = js::overview(e);
        const auto* h2 = find(ev, "xpg-hugo");
        const auto* l2 = find(ev, "xpg-lea");
        check(h2 && !h2->chain.ok() && h2->chain.verified == h2->chain.lines - 1 && h2->chain.firstBreak > 0 && !ev.chain.ok() && l2 && !l2->chain.ok(),
              "une ligne de Hugo modifi\xC3\xA9" "e : sa cha\xC3\xAEne et celle du journal rompues (et celle des lignes d'apr\xC3\xA8s)");
    }
    // ---- un autre fuseau (UTC) : les heures suivent ----
    {
        js::Inputs e = in;
        e.tz = js::TimeZone::fixed(0);
        const auto s = js::adminStats(e, "xpg-hugo");
        check(s.dayHour[6][22] == 4, "en UTC : les 4 actions de Hugo le dimanche a 22 h (23 h a Paris)");
    }
    // ---- depuis l'Admin : la vue du journal et la liste signee ----
    {
        license_admin::gui::JournalView view;
        std::string prevHash;
        for (std::uint64_t n : {1u, 2u, 4u}) {
            license_admin::gui::JournalRow r;
            r.line.number = n;
            r.line.previous = n == 1 ? std::string() : prevHash;
            r.line.hash = std::string(64, static_cast<char>('a' + n));
            r.line.key = kL;
            r.line.parsed.when = paris(11, 2, 9, static_cast<int>(n));
            r.line.parsed.action = "courriel";
            r.verified = true;
            r.who = "L\xC3\xA9" "a Essai";
            prevHash = r.line.hash;
            view.rows.push_back(r);
        }
        const auto lines = js::linesOf(view);
        check(lines.size() == 3 && lines[0].chainOk && lines[1].chainOk && !lines[2].chainOk && lines[1].action == "courriel" && lines[2].number == 4,
              "linesOf : la cha\xC3\xAEne relue (la ligne 3 manque : rompue \xC3\xA0 la 4)");
        licensing::admins::AdminList list;
        licensing::admins::Admin a;
        a.id = "xpg-lea";
        a.key = kL;
        a.name = "L\xC3\xA9" "a Essai";
        a.rights = {"courriels", "licence_creer"};
        list.admins.push_back(a);
        const auto admins = js::adminsOf(list, {kC}, "Camille Essai");
        check(admins.size() == 2 && admins[0].plus && admins[0].keys == std::vector<std::string>{kC} && admins[0].rights.size() == 13 && admins[1].keys[0] == kL &&
                  admins[1].rights.front() == "licence_creer",
              "adminsOf : l'admin+ en t\xC3\xAAte (ses cl\xC3\xA9s), puis la liste (les droits dans l'ordre)");
    }

    std::printf("%d verification(s), %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
