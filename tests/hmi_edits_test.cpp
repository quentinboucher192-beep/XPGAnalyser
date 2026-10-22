// =============================================================================
//  tests/hmi_edits_test.cpp - 1.12.25 : l'historique des modifications des objets
// -----------------------------------------------------------------------------
//  Ce qui est verifie :
//    - la comparaison d'un objet, propriete par propriete (la geometrie, l'apparence,
//      les liaisons et expressions, les actions, l'objet lui-meme) ;
//    - revenir : chaque changement remis (avant, apres), l'objet redevient le meme ;
//    - les entrees d'un enregistrement : modifie, cree (d'ou il vient), supprime (son
//      dernier etat, qui se restaure) ; rien quand rien n'a change ;
//    - le fichier : relu tel qu'ecrit ;
//    - 100 entrees par objet : la creation reste, les plus anciennes se regroupent ;
//    - sur le disque : l'enregistrement compare chaque vue a son fichier, signe, garde ;
//      un Enregistrer sous emporte l'historique.
// =============================================================================
#include "../src/core/Version.hpp"
#include "../src/hmi/HmiEditHistory.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace hmi;
using namespace hmi::edits;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    if (!ok) std::printf("ECHEC  %s\n", what.c_str());
}

const Change* find(const std::vector<Change>& v, std::string_view key) {
    for (const auto& c : v) if (c.key == key) return &c;
    return nullptr;
}

Signature sig(std::int64_t t, std::string pc = "PC-SEATI-QB12", std::string account = "qboucher") {
    Signature s;
    s.t = t;
    s.pc = std::move(pc);
    s.account = std::move(account);
    s.version = "1.12.25";
    return s;
}

struct Fixture {
    Project p;
    Id      view{kNoId};
    Fixture() {
        auto v = makeView(p, "Vue_Reseau_AMF");
        view = v.id;
        const Id layer = v.activeLayer;
        v.objects.push_back(makeObject(Kind::Damper, p.allocate(), "RG_TE3212_1", 100, 200, layer));
        v.objects.push_back(makeObject(Kind::Fan, p.allocate(), "VE_TE3212_1", 300, 200, layer));
        v.objects.push_back(makeObject(Kind::Text, p.allocate(), "TXT_1", 40, 40, layer));
        p.views.push_back(std::move(v));
    }
    View& v() { return p.views.front(); }
    Object& obj(std::size_t i) { return v().objects[i]; }
};

void comparer() {
    std::printf("-- comparer un objet\n");
    Fixture f;
    const Object before = f.obj(0);
    Object after = before;
    check(diff(before, after).empty(), "le meme objet : aucun changement");
    after.name = "RG_TE3212";
    after.set("x", "120");
    after.set("fill", "#EEF1F5");
    after.setExpr("limitOpen", "FC_RG3212_O");
    after.locked = true;
    Action a;
    a.trigger = Trigger::Click;
    a.operation = Operation::Popup;
    a.target = "Pop_Registre";
    after.actions.push_back(a);
    const std::string removedKey = "textColor";
    after.props.erase(std::find_if(after.props.begin(), after.props.end(), [&](const Prop& pr) { return pr.key == removedKey; }));   // une propriete retiree
    const auto ch = diff(before, after);
    const Change* nom = find(ch, "nom");
    const Change* x = find(ch, "x");
    const Change* fill = find(ch, "fill");
    const Change* fco = find(ch, "limitOpen");
    const Change* act = find(ch, "action 1");
    const Change* lock = find(ch, "verrou");
    check(nom && nom->sort == Sort::Object && nom->before == "RG_TE3212_1" && nom->after == "RG_TE3212", "renomm\xC3\xA9 : le nom avant et apr\xC3\xA8s, sorte Objet");
    check(x && x->sort == Sort::Geometry && x->after == "120", "x : Position et taille");
    check(fill && fill->sort == Sort::Appearance && fill->before == "#1B2028" && fill->after == "#EEF1F5", "le fond : Apparence, #1B2028 puis #EEF1F5");
    check(fco && fco->sort == Sort::Bindings && fco->afterExpr == "FC_RG3212_O" && shown(*fco, false) == "=FC_RG3212_O",
          "une expression : Liaisons et expressions, montr\xC3\xA9" "e =FC_RG3212_O");
    check(act && act->sort == Sort::Actions && act->beforeAbsent && !act->afterAbsent
              && blockSummary(*act, false).find("Pop_Registre") != std::string::npos,
          "une action ajout\xC3\xA9" "e : absente avant, en clair apr\xC3\xA8s (" + (act ? blockSummary(*act, false) : std::string()) + ")");
    check(lock && lock->before == "non" && lock->after == "oui", "verrouill\xC3\xA9 : non puis oui");
    const Change* gone = find(ch, removedKey);
    check(gone && gone->afterAbsent && !gone->beforeAbsent, "une propri\xC3\xA9t\xC3\xA9 retir\xC3\xA9" "e : absente apr\xC3\xA8s");

    std::printf("-- revenir\n");
    Object back = after;
    for (const auto& c : ch) check(apply(back, c, true), "revenir \xC3\xA0 Avant : " + c.key);
    // l'ordre des proprietes peut changer (une retiree revient a la fin) : on compare cle par cle
    bool same = back.name == before.name && back.locked == before.locked && back.actions.size() == before.actions.size()
             && back.props.size() == before.props.size();
    for (const auto& pr : before.props) {
        const Prop* q = back.find(pr.key);
        same = same && q && q->value == pr.value && q->expr == pr.expr;
    }
    check(same, "tout revenu : l'objet d'avant");
    Object forth = back;
    for (const auto& c : ch) (void)apply(forth, c, false);
    check(diff(after, forth).empty(), "et de nouveau apr\xC3\xA8s : le m\xC3\xAAme que l'objet modifi\xC3\xA9");
    // une action retiree revient a son rang
    Object two = after;
    Action b = a;
    b.target = "Pop_Autre";
    two.actions.push_back(b);
    Object one = two;
    one.actions.erase(one.actions.begin());
    const auto ch2 = diff(two, one);
    Object again = one;
    for (const auto& c : ch2) (void)apply(again, c, true);
    check(again.actions.size() == 2 && again.actions[0].target == "Pop_Registre" && again.actions[1].target == "Pop_Autre",
          "les actions remises dans l'ordre");
}

void enregistrement() {
    std::printf("-- les entr\xC3\xA9" "es d'un enregistrement\n");
    Fixture f;
    const View before = f.v();
    check(compare(&before, f.v(), sig(1000)).empty(), "rien n'a chang\xC3\xA9 : aucune entr\xC3\xA9" "e");
    f.obj(0).set("fill", "#EEF1F5");
    const Object deleted = f.obj(2);
    f.v().objects.erase(f.v().objects.begin() + 2);
    Object copy = f.obj(1);
    copy.id = f.p.allocate();
    copy.name = "VE_TE3212_2";
    f.v().objects.push_back(copy);
    Notes notes{{copy.id, "dupliqu\xC3\xA9 de VE_TE3212_1"}};
    const auto es = compare(&before, f.v(), sig(1000), notes);
    check(es.size() == 3, "trois entr\xC3\xA9" "es : modifi\xC3\xA9, cr\xC3\xA9\xC3\xA9, supprim\xC3\xA9 (" + std::to_string(es.size()) + ")");
    bool mod = false, cre = false, del = false;
    for (const auto& e : es) {
        if (e.what == What::Modified && e.object == f.obj(0).id && e.changes.size() == 1 && e.pc == "PC-SEATI-QB12" && e.account == "qboucher")
            mod = true;
        if (e.what == What::Created && e.object == copy.id && e.note == notes[copy.id]) cre = true;
        if (e.what == What::Deleted && e.object == deleted.id) {
            const auto o = fromSnapshot(e.snapshot);
            del = o && *o == deleted;
        }
    }
    check(mod, "modifi\xC3\xA9 : sign\xC3\xA9 (PC, compte), un changement");
    check(cre, "cr\xC3\xA9\xC3\xA9 : d'o\xC3\xB9 il vient (dupliqu\xC3\xA9 de VE_TE3212_1)");
    check(del, "supprim\xC3\xA9 : son dernier \xC3\xA9tat, qui se restaure tel quel");
    const auto fresh = compare(nullptr, f.v(), sig(1000));
    check(fresh.size() == f.v().objects.size() && fresh.front().what == What::Created, "une vue nouvelle : chaque objet cr\xC3\xA9\xC3\xA9");

    std::printf("-- le fichier\n");
    Log log;
    log.view = f.view;
    log.entries = es;
    log.entries.front().note = "un \"guillemet\"\net une ligne";
    const Log back = parse(serialize(log), f.view);
    check(back.view == log.view && back.entries == log.entries, "relu tel qu'\xC3\xA9" "crit (" + std::to_string(back.entries.size()) + " entr\xC3\xA9" "es)");
    check(fileOf(3) == "historique-edition/0003.txt", "le fichier d'une vue : historique-edition/0003.txt");
    check(parse("# rien\nhistorique vue=3\nentree t=12 objet=4 nom=\"A\" type=Text quoi=cree\nligne abimee \"\nfin\n", 3).entries.size() == 1,
          "une ligne ab\xC3\xAEm\xC3\xA9" "e est saut\xC3\xA9" "e, le reste se lit");
}

void garder() {
    std::printf("-- garder 100 entr\xC3\xA9" "es par objet\n");
    Log log;
    log.view = 1;
    Entry c;
    c.object = 7;
    c.t = 100;
    c.what = What::Created;
    c.pc = "PC-A";
    log.entries.push_back(c);
    for (int i = 1; i <= 129; ++i) {
        Entry e;
        e.object = 7;
        e.t = 100 + i;
        e.pc = i <= 10 ? "PC-A" : "PC-B";
        e.account = "qboucher";
        Change ch;
        ch.key = "x";
        ch.sort = Sort::Geometry;
        ch.before = std::to_string(i - 1);
        ch.after = std::to_string(i);
        e.changes.push_back(ch);
        log.entries.push_back(e);
    }
    Entry other;   // un autre objet, peu d'entrees : intact
    other.object = 8;
    other.t = 500;
    log.entries.push_back(other);
    trim(log, 100);
    int n7 = 0;
    for (const auto& e : log.entries) n7 += e.object == 7 ? 1 : 0;
    const Entry* merged = log.entries.size() > 1 ? &log.entries[1] : nullptr;
    check(n7 == 100, "100 entr\xC3\xA9" "es gard\xC3\xA9" "es (" + std::to_string(n7) + ")");
    check(log.entries.front().what == What::Created, "la cr\xC3\xA9" "ation reste en t\xC3\xAA" "te");
    // 130 entrees : la creation, 98 recentes, et une qui regroupe les 31 plus anciennes apres la creation
    check(merged && merged->what == What::Merged && merged->count == 31 && merged->since == 101 && merged->t == 131,
          "les 31 plus anciennes regroup\xC3\xA9" "es en une, du 1er au 31e enregistrement");
    check(merged && merged->changes.size() == 1 && merged->changes[0].before == "0" && merged->changes[0].after == "31",
          "regroup\xC3\xA9" "es : x de 0 \xC3\xA0 31");
    check(merged && merged->pc == "PC-A, PC-B", "regroup\xC3\xA9" "es : les PC qui y ont travaill\xC3\xA9 (" + (merged ? merged->pc : std::string()) + ")");
    check(countOf(log, 7) == 130 && lastOf(log, 7) && lastOf(log, 7)->t == 229 && createdOf(log, 7), "130 enregistrements en tout ; le dernier, la cr\xC3\xA9" "ation");
    check(lastOf(log, 8) && lastOf(log, 8)->t == 500, "l'autre objet, intact");
}

void surLeDisque() {
    std::printf("-- sur le disque\n");
    const fs::path root = fs::temp_directory_path() / "xpg_hmi_edits_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string a = (root / "A").string(), b = (root / "B").string();
    Fixture f;
    check(static_cast<bool>(save(f.p, a)), "le projet \xC3\xA9" "crit");
    auto none = record(f.p, a, a, sig(2000));
    check(none && *none == 0, "rien n'a chang\xC3\xA9 : rien d'\xC3\xA9" "crit");
    f.obj(0).name = "RG_TE3212";
    f.obj(1).set("fill", "#2E7D32");
    auto n = record(f.p, a, a, sig(2000, "PC-XFAB-B32", "mmartin"));
    check(n && *n == 2, "deux objets chang\xC3\xA9s : deux entr\xC3\xA9" "es (" + std::to_string(n ? *n : -1) + ")");
    Log log = load(a, f.view);
    check(log.entries.size() == 2 && log.entries[0].pc == "PC-XFAB-B32" && log.entries[0].account == "mmartin" && renamed(log.entries[0]),
          "relues du fichier : sign\xC3\xA9" "es PC-XFAB-B32, mmartin ; renomm\xC3\xA9");
    check(fs::exists(fs::path(a) / "ihm" / fileOf(f.view)), "ihm/historique-edition/<vue>.txt dans le projet");
    check(static_cast<bool>(save(f.p, a)), "enregistr\xC3\xA9");
    auto zero = record(f.p, a, a, sig(2100));
    check(zero && *zero == 0, "enregistr\xC3\xA9 de nouveau sans changement : rien");
    // Enregistrer sous : l'historique suit, la comparaison se fait avec le dernier enregistrement
    f.obj(2).set("text", "Collecteur Nord");
    auto m = record(f.p, a, b, sig(2200));
    Log moved = load(b, f.view);
    check(m && *m == 1 && moved.entries.size() == 3, "Enregistrer sous : l'historique copi\xC3\xA9, une entr\xC3\xA9" "e de plus (" + std::to_string(moved.entries.size()) + ")");
    const auto saved = savedView(a, f.view);
    check(saved && saved->objects.size() == 3 && saved->objects[0].name == "RG_TE3212", "la vue au dernier enregistrement : son fichier");
    fs::remove_all(root, ec);
}

void signature() {
    std::printf("-- la signature\n");
    const Signature s = currentSignature();
    check(!s.pc.empty() && s.version == XPG_ANALYZER_VERSION && s.t > 1700000000, "ce PC (" + s.pc + "), la version, maintenant");
    check(dateText(0).size() == 16, "une date : jj/mm/aaaa hh:mm");
    check(sortLabel(Sort::Bindings) == "Liaisons et expressions" && whatLabel(What::Deleted) == "Suppression", "les libell\xC3\xA9s");
}

} // namespace

int main() {
    comparer();
    enregistrement();
    garder();
    surLeDisque();
    signature();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
