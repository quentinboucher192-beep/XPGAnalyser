// =============================================================================
//  hmi/HmiViewCycle.cpp - voir HmiViewCycle.hpp
// =============================================================================
#include "HmiViewCycle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace hmi::viewcycle {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameName(std::string_view a, std::string_view b) { return lowered(a) == lowered(b); }

// Le premier nombre d'un texte ("1 cycle sur 7" : 7 apres "sur" ; "7" : 7) ; 0 : aucun.
int numberAfter(std::string_view s, std::string_view word) {
    std::size_t from = 0;
    if (!word.empty()) {
        const auto at = lowered(s).find(word);
        if (at == std::string::npos) return 0;
        from = at + word.size();
    }
    while (from < s.size() && !std::isdigit(static_cast<unsigned char>(s[from]))) ++from;
    if (from >= s.size()) return 0;
    long v = 0;
    while (from < s.size() && std::isdigit(static_cast<unsigned char>(s[from])) && v < 100000) v = v * 10 + (s[from++] - '0');
    return static_cast<int>(v);
}

} // namespace

const std::array<Step, kSteps>& steps() noexcept {
    static const std::array<Step, kSteps> all = {{
        {1, "Lecture des \xC3\xA9quipements", "Lecture", "variables li\xC3\xA9" "es (%MW, Modbus), leur qualit\xC3\xA9",
         "Au d\xC3\xA9" "but du cycle, le moteur relit la table des variables li\xC3\xA9" "es (une liaison ajout\xC3\xA9" "e ou retir\xC3\xA9" "e compte tout de "
         "suite) et suit les variables des alarmes. Les valeurs, elles, arrivent par la t\xC3\xA2" "che de communication de chaque "
         "\xC3\xA9quipement, \xC3\xA0 sa p\xC3\xA9riode : un script lit toujours la derni\xC3\xA8re valeur re\xC3\xA7ue, et sa qualit\xC3\xA9.",
         "il lit les variables li\xC3\xA9" "es avant les gestes et les minuteries du cycle"},
        {2, "Entr\xC3\xA9" "es", "Entr\xC3\xA9" "es", "clics, touches, appui long, gliss\xC3\xA9s",
         "Les gestes tenus : l'appui long arriv\xC3\xA9 \xC3\xA0 sa dur\xC3\xA9" "e, les touches tenues et r\xC3\xA9p\xC3\xA9t\xC3\xA9" "es, le bouton \xC3\xA0 confirmation "
         "maintenu. Un clic, une touche, un gliss\xC3\xA9 partent d\xC3\xA8s qu'ils arrivent, entre deux cycles : pas ici.",
         "il voit les gestes tenus de ce cycle (appui long, touches)"},
        {3, "Minuteries", "Minuteries", "IHM_APRES, IHM_CHAQUE, IHM_SUR_CHANGEMENT",
         "IHM_APRES et IHM_CHAQUE arriv\xC3\xA9s \xC3\xA0 leur heure, IHM_SUR_CHANGEMENT, la tranche des scripts d\xC3\xA9tach\xC3\xA9s, les actions "
         "Minuterie de la vue et de ses objets.",
         "il voit ce que les minuteries viennent de faire"},
        {4, "Scripts g\xC3\xA9n\xC3\xA9raux cycliques", "Scripts g\xC3\xA9n\xC3\xA9raux", "Programmation g\xC3\xA9n\xC3\xA9rale (\xC3\xA0 leur p\xC3\xA9riode)",
         "Les scripts g\xC3\xA9n\xC3\xA9raux Cyclique arriv\xC3\xA9s \xC3\xA0 leur p\xC3\xA9riode, et Sur changement dont l'expression a chang\xC3\xA9. Juste avant "
         "eux : les m\xC3\xA9" "dias (GIF, vid\xC3\xA9os, documents, calculatrices). Apr\xC3\xA8s 4, c'est la place de l'OnCycle d'avant.",
         "comme l'OnCycle d'avant : apr\xC3\xA8s les scripts g\xC3\xA9n\xC3\xA9raux, avant les d\xC3\xA9" "clencheurs"},
        {5, "D\xC3\xA9" "clencheurs des objets", "D\xC3\xA9" "clencheurs", "fronts, changements \xE2\x86\x92 actions",
         "Les actions Front montant, Front descendant et Sur changement de la vue et de ses objets, quand leur expression "
         "a chang\xC3\xA9. Vue par vue : la vue, puis chaque popup ouverte - un script plac\xC3\xA9 apr\xC3\xA8s 4 ou apr\xC3\xA8s 5 tourne avec sa "
         "vue, juste avant ou juste apr\xC3\xA8s ses d\xC3\xA9" "clencheurs.",
         "il voit ce que les actions des objets viennent d'\xC3\xA9" "crire"},
        {6, "Alarmes", "Alarmes", "conditions, sons, mises de c\xC3\xB4t\xC3\xA9",
         "Les conditions des alarmes (apparition, disparition, d\xC3\xA9lai), leurs sons r\xC3\xA9p\xC3\xA9t\xC3\xA9s, la fin des mises de c\xC3\xB4t\xC3\xA9.",
         "il voit les alarmes de ce cycle (apparues, disparues)"},
        {7, "Courbes, archives, compteurs", "Courbes", "\xC3\xA9" "chantillons, PID, production, rapports",
         "Les \xC3\xA9" "chantillons des courbes, des graphiques et des archives, les compteurs horaires, la production, les "
         "r\xC3\xA9glages PID, les \xC3\xA9" "diteurs de recettes, les rapports \xC3\xA0 leur heure. Le travail du moteur s'arr\xC3\xAAte l\xC3\xA0.",
         "il voit les courbes, les archives et les compteurs de ce cycle"},
        {8, "Formules des objets (fx)", "Formules", "couleurs, textes, visibilit\xC3\xA9, styles calcul\xC3\xA9s",
         "L'\xC3\xA9" "cran de la simulation calcule les formules des objets de la vue montr\xC3\xA9" "e (et de ses popups). Un script plac\xC3\xA9 "
         "apr\xC3\xA8s 8 tourne ensuite ; s'il a tourn\xC3\xA9, l'\xC3\xA9" "cran recalcule la vue avant de la dessiner : ce qu'il change se voit "
         "dans ce cycle. Sans \xC3\xA9" "cran (un essai sans affichage), 8 et 9 suivent 7.",
         "tes modifications d'objets seront dessin\xC3\xA9" "es dans ce cycle"},
        {9, "Dessin de la vue", "Dessin", "fond \xE2\x86\x92 calques \xE2\x86\x92 objets \xE2\x86\x92 popups",
         "L'\xC3\xA9" "cran dessine la vue : le fond, les calques, les objets, les popups. Un script plac\xC3\xA9 apr\xC3\xA8s 9 tourne le dessin "
         "fait, avant le cycle suivant : ce qu'il change se voit au cycle suivant.",
         "ce qu'il change se voit au cycle suivant"},
        {10, "\xC3\x89" "critures envoy\xC3\xA9" "es", "\xC3\x89" "critures", "ce que le cycle a \xC3\xA9" "crit part aux \xC3\xA9quipements",
         "Une \xC3\xA9" "criture part d\xC3\xA8s qu'un script ou une action la fait : une variable IHM change tout de suite, l'automate "
         "aussi ; pour un \xC3\xA9quipement, elle est remise \xC3\xA0 sa liaison, que la t\xC3\xA2" "che de communication envoie \xC3\xA0 son prochain "
         "passage, avant ses lectures. Ce n'est donc pas une \xC3\xA9tape \xC3\xA0 part, et rien ne se place apr\xC3\xA8s 10 (c'est avant 1 du "
         "cycle suivant). Le chiffre : les \xC3\xA9" "critures du dernier cycle.",
         ""},
    }};
    return all;
}

const Step& step(int number) noexcept { return steps()[static_cast<std::size_t>(std::clamp(number, 1, kSteps) - 1)]; }

bool isCycleScript(const Script& s) noexcept { return s.event == kCycleEvent; }
int momentOf(const Script& s) noexcept { return std::clamp(s.stage, kFirstMoment, kLastMoment); }
int everyOf(const Script& s) noexcept { return std::clamp(s.everyCycles, 1, kMaxEvery); }
bool customized(const Script& s) noexcept { return momentOf(s) != kLegacyMoment || everyOf(s) != 1 || !trimmed(s.when).empty(); }

std::string momentLabel(int moment) {
    const auto& st = step(moment);
    return "apr\xC3\xA8s l'\xC3\xA9tape " + std::to_string(st.number) + " (" + std::string(st.shortTitle) + ")";
}

std::vector<std::string> momentChoices() {
    std::vector<std::string> out;
    for (int m = kFirstMoment; m <= kLastMoment; ++m) out.push_back("Apr\xC3\xA8s l'\xC3\xA9tape " + std::to_string(m) + " : " + std::string(step(m).title));
    return out;
}

int momentFromText(std::string_view text) {
    const auto choices = momentChoices();
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == text) return static_cast<int>(i) + kFirstMoment;
    const int n = numberAfter(text, {});
    return n >= kFirstMoment && n <= kLastMoment ? n : 0;
}

std::string everyLabel(int every) {
    every = std::clamp(every, 1, kMaxEvery);
    return every == 1 ? std::string("Chaque cycle") : "1 cycle sur " + std::to_string(every);
}

std::vector<std::string> everyChoices() {
    std::vector<std::string> out;
    for (const int n : {1, 2, 3, 5, 10, 20, 50, 100}) out.push_back(everyLabel(n));
    return out;
}

int everyFromText(std::string_view text) {
    const std::string t = lowered(trimmed(text));
    if (t.empty()) return 0;
    if (t == "chaque cycle" || t == "chaque" || t == "toujours") return 1;
    const int n = t.find("sur") != std::string::npos ? numberAfter(t, "sur") : numberAfter(t, {});
    return n >= 1 && n <= kMaxEvery ? n : 0;
}

std::string plainCondition(std::string_view text) {
    std::string t = trimmed(text);
    while (!t.empty() && t.front() == '=') t = trimmed(std::string_view(t).substr(1));
    return t;
}

std::string summary(const Script& s) {
    std::string out = momentLabel(momentOf(s));
    if (everyOf(s) > 1) out += " \xC2\xB7 " + everyLabel(everyOf(s));
    if (const auto c = plainCondition(s.when); !c.empty()) out += " \xC2\xB7 si =" + c;
    return out;
}

std::string badge(const Script& s) {
    std::string out = "apr\xC3\xA8s " + std::to_string(momentOf(s));
    if (everyOf(s) > 1) out += " \xC2\xB7 1/" + std::to_string(everyOf(s));
    if (!plainCondition(s.when).empty()) out += " \xC2\xB7 si";
    return out;
}

std::string dropText(int moment) {
    moment = std::clamp(moment, kFirstMoment, kLastMoment);
    const auto& a = step(moment);
    const auto& b = step(moment + 1);
    return "D\xC3\xA9poser ici : apr\xC3\xA8s " + std::to_string(a.number) + " " + std::string(a.shortTitle) + ", avant " + std::to_string(b.number) + " "
         + std::string(b.shortTitle) + " \xE2\x80\x94 " + std::string(a.after);
}

std::vector<const Script*> cycleScripts(const View& v) {
    std::vector<const Script*> out;
    for (int m = kFirstMoment; m <= kLastMoment; ++m)
        for (const auto& s : v.scripts)
            if (isCycleScript(s) && momentOf(s) == m) out.push_back(&s);
    return out;
}

std::vector<const Script*> atMoment(const View& v, int moment) {
    std::vector<const Script*> out;
    for (const auto& s : v.scripts)
        if (isCycleScript(s) && momentOf(s) == moment) out.push_back(&s);
    return out;
}

std::pair<int, int> rankOf(const View& v, Id script) {
    const Script* me = nullptr;
    for (const auto& s : v.scripts)
        if (s.id == script) me = &s;
    if (!me || !isCycleScript(*me)) return {0, 0};
    const auto same = atMoment(v, momentOf(*me));
    for (std::size_t i = 0; i < same.size(); ++i)
        if (same[i] == me) return {static_cast<int>(i) + 1, static_cast<int>(same.size())};
    return {0, 0};
}

bool move(View& v, Id script, int moment, std::size_t rank) {
    moment = std::clamp(moment, kFirstMoment, kLastMoment);
    const auto at = std::find_if(v.scripts.begin(), v.scripts.end(), [&](const Script& s) { return s.id == script; });
    if (at == v.scripts.end() || !isCycleScript(*at)) return false;
    const std::vector<Script> before = v.scripts;
    Script moved = *at;
    moved.stage = moment;
    v.scripts.erase(at);
    // Les autres scripts de ce moment, dans leur ordre : le deplace se glisse avant le `rank`-ieme ; au-dela, apres le
    // dernier ; aucun : apres le dernier script de cycle d'un moment plus tot (sinon a la place du premier plus tard,
    // sinon a la fin).
    std::vector<std::size_t> peers;
    for (std::size_t i = 0; i < v.scripts.size(); ++i)
        if (isCycleScript(v.scripts[i]) && momentOf(v.scripts[i]) == moment) peers.push_back(i);
    std::size_t pos = v.scripts.size();
    if (!peers.empty()) {
        pos = rank < peers.size() ? peers[rank] : peers.back() + 1;
    } else {
        std::size_t earlier = v.scripts.size(), later = v.scripts.size();
        for (std::size_t i = 0; i < v.scripts.size(); ++i) {
            if (!isCycleScript(v.scripts[i])) continue;
            if (momentOf(v.scripts[i]) < moment) earlier = i;
            else if (later == v.scripts.size()) later = i;
        }
        pos = earlier < v.scripts.size() ? earlier + 1 : later;
    }
    v.scripts.insert(v.scripts.begin() + static_cast<std::ptrdiff_t>(std::min(pos, v.scripts.size())), std::move(moved));
    return !(v.scripts == before);
}

std::string freeName(const View& v, std::string_view base) {
    const std::string b = base.empty() ? std::string("Cycle") : std::string(base);
    const auto taken = [&](const std::string& n) {
        return std::any_of(v.scripts.begin(), v.scripts.end(), [&](const Script& s) { return sameName(s.name, n); });
    };
    if (!taken(b)) return b;
    for (int i = 2; i < 10000; ++i)
        if (const std::string n = b + "_" + std::to_string(i); !taken(n)) return n;
    return b;
}

bool defaultName(const Script& s, std::string_view viewName) {
    // Les noms que l'editeur donnait (Vue_OnCycle a la frappe, Vue.OnCycle a l'import, Pompe_1.Sym_OnCycle dans une
    // instance de symbole) : l'evenement, seul ou au bout du nom.
    const std::string ev(s.event), name = lowered(s.name), tail = lowered(ev);
    const auto endsWith = [&](const std::string& suffix) {
        return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return s.name.empty() || name == tail || sameName(s.name, std::string(viewName) + "_" + ev) || endsWith("_" + tail) || endsWith("." + tail);
}

std::string msText(double ms) {
    if (ms < 0) return "\xE2\x80\x94";
    char b[48];
    std::snprintf(b, sizeof b, ms < 10 ? "%.2f ms" : "%.1f ms", ms);
    std::string s = b;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

} // namespace hmi::viewcycle
