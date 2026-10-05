// =============================================================================
//  app/SimStatus.cpp - lot API 8 : l'etat de la simulation, dit en clair
// =============================================================================
#include "SimStatus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app::simstatus {

namespace {

using State = PlcFacts::State;

std::string decimal1(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string s = b;
    for (auto& c : s)
        if (c == '.') c = ',';
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ",0") == 0) s.resize(s.size() - 2);
    return s;
}

// "a, b et c".
std::string joinList(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += i + 1 == parts.size() ? " et " : ", ";
        out += parts[i];
    }
    return out;
}

std::string lineKey(const std::string& section, std::uint32_t line) {
    return "ligne:" + section + ":" + std::to_string(line);
}

std::string place(const std::string& section, std::uint32_t line) {
    if (section.empty()) return {};
    return line ? section + ", ligne " + std::to_string(line) : section;
}

double cycleMs(const PlcFacts& p) { return static_cast<double>(p.scanMicros) / 1000.0; }

// Les equipements qui ne repondent pas : actifs, essayes, muets.
std::vector<const EquipFacts::One*> silent(const EquipFacts& e) {
    std::vector<const EquipFacts::One*> out;
    for (const auto& one : e.list)
        if (one.enabled && one.tested && !one.ok) out.push_back(&one);
    return out;
}

std::size_t enabledCount(const EquipFacts& e) {
    return static_cast<std::size_t>(std::count_if(e.list.begin(), e.list.end(), [](const EquipFacts::One& o) { return o.enabled; }));
}

std::size_t simulatedCount(const EquipFacts& e) {
    return static_cast<std::size_t>(
        std::count_if(e.list.begin(), e.list.end(), [](const EquipFacts::One& o) { return o.enabled && o.simulated; }));
}

std::string unknownNames(const PlcFacts& p, std::size_t max) {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < p.unknown.size() && i < max; ++i) names.push_back(p.unknown[i].name);
    std::string out = joinList(names);
    if (p.unknown.size() > max) out += " (+ " + std::to_string(p.unknown.size() - max) + ")";
    return out;
}

std::uint64_t unknownCalls(const PlcFacts& p) {
    std::uint64_t n = 0;
    for (const auto& u : p.unknown) n += u.calls;
    return n;
}

// Ce que dit le bandeau quand quelque chose est a surveiller - dans l'ordre :
// ce qui fausse le calcul, ce qui coupe une liaison, ce qui plante un script.
struct Concern {
    std::string title, meaning;
    Action      fix;
};

std::vector<Concern> concerns(const Snapshot& s) {
    std::vector<Concern> out;
    const auto& p = s.plc;
    if (p.prepared && p.continueUnknown && !p.unknown.empty()) {
        Concern c;
        const auto n = p.unknown.size();
        c.title = n == 1 ? p.unknown.front().name + " n'est pas simul\xC3\xA9" "e et rend 0"
                         : std::to_string(n) + " fonctions ne sont pas simul\xC3\xA9" "es et rendent 0";
        c.meaning = "Le programme tourne, mais ce que " + std::string(n == 1 ? "cette fonction calcule" : "ces fonctions calculent")
                  + " vaut 0 (" + unknownNames(p, 3) + ") : la suite du calcul peut \xC3\xAAtre fausse. Le simulateur ne les conna\xC3\xAEt pas ; "
                    "l'automate, si.";
        const auto& first = p.unknown.front();
        c.fix = !first.section.empty() ? Action{"Voir le premier appel", lineKey(first.section, first.line)} : Action{"Voir", "automate"};
        out.push_back(std::move(c));
    }
    const auto mute = silent(s.equip);
    if (!mute.empty()) {
        Concern c;
        c.title = mute.size() == 1 ? mute.front()->name + " ne r\xC3\xA9pond pas"
                                   : std::to_string(mute.size()) + " \xC3\xA9quipements ne r\xC3\xA9pondent pas";
        c.meaning = "L'IHM ne parle plus \xC3\xA0 " + (mute.size() == 1 ? mute.front()->name : std::string("ces \xC3\xA9quipements"))
                  + (mute.front()->why.empty() ? std::string{} : " (" + mute.front()->why + ")")
                  + " : leurs variables deviennent mauvaises, les vues le montrent d'une croix rouge.";
        c.fix = {"Ouvrir les \xC3\xA9quipements", "equipements"};
        out.push_back(std::move(c));
    }
    if (s.hmi.running && s.hmi.scriptErrors > 0) {
        Concern c;
        c.title = s.hmi.scriptErrors == 1 ? "un script de l'IHM s'est arr\xC3\xAAt\xC3\xA9 sur une erreur"
                                          : std::to_string(s.hmi.scriptErrors) + " erreurs de scripts dans l'IHM";
        c.meaning = "Ce que le script devait faire n'a pas \xC3\xA9t\xC3\xA9 fait"
                  + (s.hmi.lastScriptError.empty() ? std::string(".") : " : " + s.hmi.lastScriptError + ".");
        c.fix = {"Voir le journal", "journal"};
        out.push_back(std::move(c));
    }
    return out;
}

Banner banner(const Snapshot& s) {
    Banner b;
    const auto& p = s.plc;
    if (!s.project) {
        b.tone = Tone::Off;
        b.word = "AUCUN PROJET";
        b.title = "Aucun projet ouvert";
        b.meaning = "La simulation fait tourner le programme du projet ouvert, son IHM et ses \xC3\xA9quipements : ouvre d'abord un projet.";
        return b;
    }
    if (!p.prepared && !p.prepareError.empty()) {
        b.tone = Tone::Error;
        b.word = "PAS PR\xC3\x8ATE";
        b.title = "La simulation ne d\xC3\xA9marre pas : " + p.prepareError;
        b.meaning = "Le simulateur n'a pas pu pr\xC3\xA9parer le programme de MAST : rien ne tourne tant que ce n'est pas corrig\xC3\xA9.";
        b.fix = {"R\xC3\xA9" "essayer", "sim.run"};
        return b;
    }
    if (p.prepared && p.state == State::Halted) {
        b.tone = Tone::Error;
        b.word = "ARR\xC3\x8AT\xC3\x89";
        const std::string where = !p.haltBlock.empty() ? p.haltBlock : p.haltSection;
        if (p.haltLoop && !where.empty()) {
            b.title = "L'automate est arr\xC3\xAAt\xC3\xA9 : boucle sans fin dans " + where + (p.haltLine ? ", ligne " + std::to_string(p.haltLine) : std::string{})
                    + " (" + (p.haltLimitText.empty() ? "le cycle a d\xC3\xA9pass\xC3\xA9 " + milliseconds(static_cast<double>(p.haltLimitMs))
                                                        : p.haltLimitText.rfind("la boucle", 0) == 0 ? p.haltLimitText
                                                                                                      : "le cycle a d\xC3\xA9pass\xC3\xA9 " + p.haltLimitText)
                    + ")";
        } else {
            b.title = "L'automate est arr\xC3\xAAt\xC3\xA9 : " + (p.haltReason.empty() ? std::string("un d\xC3\xA9" "faut a arr\xC3\xAAt\xC3\xA9 le cycle") : p.haltReason);
            if (const auto at = place(p.haltSection, p.haltLine); !at.empty() && p.haltReason.find(p.haltSection) == std::string::npos)
                b.title += " (" + at + ")";
        }
        b.meaning = "Un d\xC3\xA9" "faut a arr\xC3\xAAt\xC3\xA9 le programme au cycle " + thousands(p.cycle)
                  + " : rien ne bouge plus, les sorties gardent leur derni\xC3\xA8re valeur. Corrige la cause, puis relance depuis z\xC3\xA9ro.";
        if (!p.haltNewer.empty())
            b.meaning += " La biblioth\xC3\xA8que a une version plus r\xC3\xA9" "cente de " + p.haltBlock + " (" + p.haltNewer + ") : elle corrige peut-\xC3\xAAtre ce d\xC3\xA9" "faut.";
        if (!p.haltNewer.empty() && !p.haltBlock.empty()) b.fix = {"Mettre \xC3\xA0 jour " + p.haltBlock + " " + p.haltNewer, "bibliotheque"};
        else if (!p.haltSection.empty()) b.fix = {"Aller \xC3\xA0 la ligne", lineKey(p.haltSection, p.haltLine)};
        else b.fix = {"Relancer depuis z\xC3\xA9ro", "relancer"};
        if (b.fix.key != "relancer") b.second = {"Relancer depuis z\xC3\xA9ro", "relancer"};
        return b;
    }
    if (p.prepared && p.breakHit) {
        b.tone = Tone::Warning;                   // la maquette : orange au point d'arret
        b.word = "POINT D'ARR\xC3\x8AT";
        b.title = "En pause sur un point d'arr\xC3\xAAt : " + place(p.breakSection, static_cast<std::uint32_t>(std::max(0, p.breakLine)))
                + " (cycle " + thousands(p.cycle) + ")";
        b.meaning = "Le programme s'est arr\xC3\xAAt\xC3\xA9 juste avant cette ligne : les valeurs sont celles de ce moment. Continuer reprend jusqu'au "
                    "prochain point d'arr\xC3\xAAt.";
        b.fix = {"D\xC3\xA9" "boguer", "debogage"};
        b.second = {"Continuer", "sim.run"};
        return b;
    }
    if (p.prepared && p.online && p.onlineAgeSeconds < 20.0) {
        b.tone = p.onlineFailed ? Tone::Error : Tone::Info;
        b.word = p.onlineFailed ? "MODIFICATION REFUS\xC3\x89" "E" : "MODIFI\xC3\x89 EN LIGNE";
        b.title = (p.onlineFailed ? "La modification en ligne n'est pas pass\xC3\xA9" "e : " : "Modification en ligne : ")
                + (p.onlineSummary.empty() ? std::string("le programme a chang\xC3\xA9 pendant que \xC3\xA7" "a tournait") : p.onlineSummary);
        b.meaning = p.onlineFailed ? "Le programme modifi\xC3\xA9 n'a pas pu remplacer celui qui tourne : la simulation continue sur l'ancien code."
                                   : "Le programme a chang\xC3\xA9 sans arr\xC3\xAAter la simulation : les valeurs sont gard\xC3\xA9" "es, le nouveau code "
                                     "tourne depuis le cycle suivant.";
        b.fix = {"Voir le journal", "journal"};
        return b;
    }
    if (p.prepared && p.stale && p.state == State::Running) {
        b.tone = Tone::Info;
        b.word = "ANCIEN CODE";
        b.title = "Le programme a chang\xC3\xA9 : la simulation tourne encore sur l'ancien code";
        b.meaning = "Tes derni\xC3\xA8res modifications ne sont pas prises tant que la simulation n'est pas relanc\xC3\xA9" "e : Relancer arr\xC3\xAAte puis simule "
                    "le nouveau code (tout repart de z\xC3\xA9ro).";
        b.fix = {"Relancer sur le nouveau code", "relancer"};
        return b;
    }
    if (p.prepared && p.state == State::Running) {
        const auto list = concerns(s);
        if (!list.empty()) {
            b.tone = Tone::Warning;
            b.word = "\xC3\x80 SURVEILLER";
            b.title = "\xC3\x80 surveiller : " + list.front().title;
            if (list.size() == 2) b.title += " (et un autre point)";
            else if (list.size() > 2) b.title += " (et " + std::to_string(list.size() - 1) + " autres points)";
            b.meaning = list.front().meaning;
            b.fix = list.front().fix;
            if (list.size() > 1) b.second = {"Tout voir", "ensemble"};
            return b;
        }
        b.tone = Tone::Ok;
        b.word = "TOUT TOURNE";
        std::vector<std::string> parts;
        std::string plc = "l'automate (cycle " + thousands(p.cycle);
        if (p.scanMicros > 0) plc += ", " + milliseconds(cycleMs(p)) + " sur " + std::to_string(p.periodMs);
        plc += ")";
        parts.push_back(plc);
        if (s.hmi.running) parts.push_back("l'IHM (" + (s.hmi.view.empty() ? std::string("en marche") : s.hmi.view) + ")");
        const auto sims = simulatedCount(s.equip);
        const auto all = enabledCount(s.equip);
        if (all > 0) parts.push_back(sims == all ? plural(all, "\xC3\xA9quipement simul\xC3\xA9", "\xC3\xA9quipements simul\xC3\xA9s") : plural(all, "\xC3\xA9quipement", "\xC3\xA9quipements"));
        b.title = "Tout tourne : " + joinList(parts);
        b.meaning = "Le programme s'ex\xC3\xA9" "cute cycle apr\xC3\xA8s cycle : les valeurs se lisent partout, au survol, dans les tables et les courbes.";
        if (p.speed == 0) b.meaning += " Au plus vite : le temps simul\xC3\xA9 va plus vite que l'horloge.";
        else if (p.speed > 1) b.meaning += " \xC3\x80 x" + std::to_string(p.speed) + " : le temps simul\xC3\xA9 va " + std::to_string(p.speed) + " fois plus vite que l'horloge.";
        if (s.hmi.exists && !s.hmi.running) {
            b.meaning += " L'IHM ne tourne pas : Lancer l'IHM la fait tourner sur l'automate simul\xC3\xA9.";
            b.fix = {"Lancer l'IHM", "ihm"};
        } else {
            b.fix = {"Voir les courbes", "courbes"};
        }
        return b;
    }
    if (p.prepared && p.state == State::Paused) {
        b.tone = Tone::Info;                      // la maquette : bleu en pause
        b.word = "EN PAUSE";
        b.title = "En pause au cycle " + thousands(p.cycle) + " : tu as appuy\xC3\xA9 sur Pause";
        b.meaning = "Rien ne bouge : l'IHM et les \xC3\xA9quipements attendent avec l'automate. Un cycle avance d'un pas ; Continuer reprend "
                    "l\xC3\xA0 o\xC3\xB9 il en \xC3\xA9tait, au m\xC3\xAAme cycle.";
        b.fix = {"Continuer", "sim.run"};
        b.second = {"Un cycle", "sim.step"};
        return b;
    }
    b.tone = Tone::Off;
    b.word = "ARR\xC3\x8AT\xC3\x89" "E";
    b.title = "La simulation est arr\xC3\xAAt\xC3\xA9" "e";
    b.meaning = "Rien ne tourne. Simuler pr\xC3\xA9pare le programme de MAST et lance les cycles : chaque variable part de sa valeur initiale.";
    if (s.hmi.exists) b.meaning += " L'IHM et les \xC3\xA9quipements simul\xC3\xA9s suivent.";
    b.fix = {"Simuler", "sim.run"};
    return b;
}

Card plcCard(const Snapshot& s) {
    Card c;
    const auto& p = s.plc;
    c.key = "automate";
    c.title = "AUTOMATE";
    c.open = "automate";
    const bool running = p.prepared && p.state == State::Running;
    const bool paused = p.prepared && p.state == State::Paused;
    const bool halted = p.prepared && p.state == State::Halted;
    if (!p.prepared && !p.prepareError.empty()) {
        c.tone = Tone::Error;
        c.state = "pas pr\xC3\xAAt";
        c.sentence = "Ne se pr\xC3\xA9pare pas : " + p.prepareError + ".";
    } else if (halted) {
        c.tone = Tone::Error;
        c.state = "arr\xC3\xAAt\xC3\xA9 (d\xC3\xA9" "faut)";
        c.sentence = "Arr\xC3\xAAt\xC3\xA9 sur un d\xC3\xA9" "faut" + (p.haltReason.empty() ? std::string(".") : " : " + p.haltReason + ".");
    } else if (running) {
        c.tone = p.continueUnknown && !p.unknown.empty() ? Tone::Warning : Tone::Ok;
        c.state = "en marche";
        c.sentence = "Il ex\xC3\xA9" "cute MAST, cycle apr\xC3\xA8s cycle";
        if (p.entries) c.sentence += " : " + plural(p.entries, "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", " + plural(p.sections, "section", "sections");
        c.sentence += p.speed == 0 ? " (au plus vite)." : p.speed > 1 ? " (\xC3\xA0 x" + std::to_string(p.speed) + ")." : ".";
    } else if (paused) {
        c.tone = Tone::Warning;
        c.state = "en pause";
        c.sentence = p.breakHit ? "En pause sur un point d'arr\xC3\xAAt : " + place(p.breakSection, static_cast<std::uint32_t>(std::max(0, p.breakLine))) + "."
                                : "En pause au cycle " + thousands(p.cycle) + " : Un cycle avance d'un pas.";
    } else {
        c.tone = Tone::Off;
        c.state = "arr\xC3\xAAt\xC3\xA9";
        c.sentence = p.prepared ? "Pr\xC3\xAAt : Simuler le lance, chaque variable \xC3\xA0 sa valeur initiale."
                                : "Pas encore pr\xC3\xA9par\xC3\xA9 : Simuler lit le programme de MAST et le lance.";
    }
    auto& f0 = c.figures[0];
    f0.label = "Cycle";
    f0.value = thousands(p.cycle);
    f0.detail = "temps simul\xC3\xA9 " + duration(static_cast<double>(p.clockMs) / 1000.0);
    auto& f1 = c.figures[1];
    f1.label = "Temps de cycle";
    const double ms = cycleMs(p);
    const double period = static_cast<double>(std::max<std::int64_t>(1, p.periodMs));
    if (p.cycle == 0 || p.scanMicros <= 0) {
        f1.value = "\xE2\x80\x94";
        f1.detail = "sur " + std::to_string(p.periodMs) + " ms (MAST)";
    } else {
        f1.value = milliseconds(ms);
        f1.detail = "sur " + std::to_string(p.periodMs) + " ms (MAST)";
        f1.bar = std::clamp(ms / period, 0.0, 1.0);
        f1.tone = ms > 2.0 * period ? Tone::Error : ms > period ? Tone::Warning : Tone::Ok;
    }
    auto& f2 = c.figures[2];
    f2.label = "For\xC3\xA7" "ages";
    f2.value = std::to_string(p.forced);
    f2.detail = p.forced == 0 ? "aucune variable forc\xC3\xA9" "e" : "le plus ancien : " + duration(p.oldestForcedSeconds);
    f2.tone = p.forced ? Tone::Warning : Tone::Off;
    // La maquette : Ouvrir l'automate, Deboguer (Simuler, Pause, Un cycle, Arreter :
    // la barre d'outils de la vue et la barre du haut ; ce qui repare : le bandeau).
    c.buttons.push_back({"Ouvrir l'automate", "automate"});
    c.buttons.push_back({"D\xC3\xA9" "boguer", "debogage"});
    return c;
}

Card hmiCard(const Snapshot& s) {
    Card c;
    const auto& h = s.hmi;
    c.key = "ihm";
    c.title = "IHM";
    c.open = "ihm";
    if (!h.exists) {
        c.tone = Tone::Off;
        c.state = "pas d'IHM";
        c.sentence = "Le projet n'a pas encore de vue : IHM \xE2\x80\xBA Vues en cr\xC3\xA9" "e une.";
    } else if (h.running) {
        c.tone = h.scriptErrors ? Tone::Error : h.unacked ? Tone::Warning : Tone::Ok;
        c.state = "en marche";
        c.sentence = "Elle tourne sur l'automate simul\xC3\xA9" + (h.view.empty() ? std::string(".") : " et affiche " + h.view + ".");
    } else {
        c.tone = Tone::Off;
        c.state = "arr\xC3\xAAt\xC3\xA9" "e";
        c.sentence = "Arr\xC3\xAAt\xC3\xA9" "e : Ouvrir l'IHM la lance sur l'automate simul\xC3\xA9, sur sa vue de d\xC3\xA9marrage.";
    }
    auto& f0 = c.figures[0];
    f0.label = "Vue affich\xC3\xA9" "e";
    f0.value = h.running && !h.view.empty() ? h.view : "\xE2\x80\x94";
    f0.detail = h.running ? "ce que voit l'op\xC3\xA9rateur" : "l'IHM ne tourne pas";
    auto& f1 = c.figures[1];
    f1.label = "Utilisateur";
    f1.value = h.running ? (h.user.empty() ? "personne" : h.user) : "\xE2\x80\x94";
    f1.detail = h.running ? (h.user.empty() ? "aucune connexion" : "connect\xC3\xA9") : std::string{};
    auto& f2 = c.figures[2];
    f2.label = "Alarmes actives";
    f2.value = h.running ? std::to_string(h.activeAlarms) : "\xE2\x80\x94";
    if (h.running) {
        f2.detail = h.unacked ? "dont " + plural(h.unacked, "\xC3\xA0 acquitter", "\xC3\xA0 acquitter") : h.alarms ? "toutes acquitt\xC3\xA9" "es" : "aucune";
        if (h.unacked == 1) f2.detail = "dont 1 \xC3\xA0 acquitter";
        else if (h.unacked > 1) f2.detail = "dont " + std::to_string(h.unacked) + " \xC3\xA0 acquitter";
        f2.tone = h.unacked ? (h.topPriority == 1 ? Tone::Error : Tone::Warning) : h.activeAlarms ? Tone::Warning : Tone::Off;
    }
    if (h.exists) c.buttons.push_back({h.running ? "Ouvrir l'IHM" : "Lancer l'IHM", "ihm"});
    if (h.running && h.alarms) c.buttons.push_back({"Voir les alarmes", "alarmes"});
    return c;
}

Card equipCard(const Snapshot& s) {
    Card c;
    const auto& e = s.equip;
    c.key = "equipements";
    c.title = "\xC3\x89QUIPEMENTS";
    c.open = "equipements";
    const auto all = enabledCount(e);
    const auto sims = simulatedCount(e);
    const auto mute = silent(e);
    std::size_t ok = 0, untested = 0;
    for (const auto& one : e.list) {
        if (!one.enabled) continue;
        if (one.ok) ++ok;
        else if (!one.tested) ++untested;
    }
    if (all == 0) {
        c.tone = Tone::Off;
        c.state = "aucun";
        c.sentence = e.list.empty() ? "Le projet n'a pas d'\xC3\xA9quipement : l'IHM ne parle qu'\xC3\xA0 l'automate."
                                    : "Aucun \xC3\xA9quipement actif : ils sont tous d\xC3\xA9sactiv\xC3\xA9s.";
    } else if (!mute.empty()) {
        c.tone = Tone::Error;
        c.state = mute.size() == 1 ? "1 ne r\xC3\xA9pond pas" : std::to_string(mute.size()) + " ne r\xC3\xA9pondent pas";
        c.sentence = mute.front()->name + " ne r\xC3\xA9pond pas" + (mute.front()->why.empty() ? std::string{} : " : " + mute.front()->why)
                   + (mute.size() > 1 ? " (et " + plural(mute.size() - 1, "autre", "autres") + ")." : ".");
    } else if (ok == all) {
        c.tone = Tone::Ok;
        c.state = ok == 1 ? "r\xC3\xA9pond" : "r\xC3\xA9pondent";
        c.sentence = all == 1 ? "L'\xC3\xA9quipement r\xC3\xA9pond" : "Les " + std::to_string(all) + " \xC3\xA9quipements r\xC3\xA9pondent";
        if (sims) c.sentence += sims == all ? (all == 1 ? " : il est lu sur son esclave simul\xC3\xA9." : " : ils sont lus sur leurs esclaves simul\xC3\xA9s.")
                                            : " (" + std::to_string(sims) + " lu" + (sims > 1 ? "s" : "") + " sur leur esclave simul\xC3\xA9).";
        else c.sentence += ".";
    } else {
        c.tone = Tone::Off;
        c.state = "pas encore essay\xC3\xA9s";
        c.sentence = plural(untested, "\xC3\xA9quipement n'a", "\xC3\xA9quipements n'ont") + " pas encore \xC3\xA9t\xC3\xA9 joint" + (untested > 1 ? "s" : "")
                   + " : leurs liaisons d\xC3\xA9marrent avec la simulation de l'IHM.";
    }
    auto& f0 = c.figures[0];
    f0.label = "\xC3\x89quipements";
    f0.value = std::to_string(all);
    f0.detail = sims ? "dont " + std::to_string(sims) + " simul\xC3\xA9" + (sims > 1 ? "s" : "") : all ? "r\xC3\xA9" "els" : "aucun";
    auto& f1 = c.figures[1];
    f1.label = "Liaisons";
    if (all == 0) {
        f1.value = "\xE2\x80\x94";
    } else {
        f1.value = std::to_string(ok) + " OK";
        if (!mute.empty()) f1.value += " \xC2\xB7 " + std::to_string(mute.size()) + " KO";
        f1.tone = !mute.empty() ? Tone::Error : ok == all ? Tone::Ok : Tone::Off;
        f1.bar = static_cast<double>(ok) / static_cast<double>(all);
    }
    f1.detail = untested ? std::to_string(untested) + " pas encore essay\xC3\xA9" + (untested > 1 ? "s" : "") : std::string("sur ") + std::to_string(all);
    auto& f2 = c.figures[2];
    f2.label = "\xC3\x89" "changes";
    f2.value = all ? decimal1(std::round(e.exchangesPerSecond * 10.0) / 10.0) + "/s" : "\xE2\x80\x94";
    f2.detail = "requ\xC3\xAAtes Modbus";
    c.buttons.push_back({"Ouvrir les \xC3\xA9quipements", "equipements"});
    return c;
}

std::array<Link, 2> chain(const Snapshot& s) {
    std::array<Link, 2> out;
    auto& a = out[0];
    a.from = "equipements";
    a.to = "automate";
    const auto all = enabledCount(s.equip);
    const auto mute = silent(s.equip);
    const std::string rate = decimal1(std::round(s.equip.exchangesPerSecond * 10.0) / 10.0) + " \xC3\xA9" "changes/s";
    if (all == 0) {
        a.idle = true;
        a.text = "aucun \xC3\xA9quipement";
        a.why = "le projet n'en a pas";
    } else if (!mute.empty()) {
        a.broken = true;
        a.text = rate;
        a.why = mute.front()->name + " ne r\xC3\xA9pond pas" + (mute.front()->why.empty() ? std::string{} : " : " + mute.front()->why);
    } else if (s.equip.exchangesPerSecond > 0.05) {
        a.alive = true;
        a.text = rate;
    } else {
        a.idle = true;
        a.text = "au repos";
        a.why = "aucun \xC3\xA9" "change en ce moment";
    }
    auto& b = out[1];
    b.from = "automate";
    b.to = "ihm";
    const auto& p = s.plc;
    const std::string reads = plural(s.hmi.plcReads, "variable lue", "variables lues");
    if (!s.hmi.exists) {
        b.idle = true;
        b.text = "pas d'IHM";
        b.why = "le projet n'a pas de vue";
    } else if (!s.hmi.running) {
        b.idle = true;
        b.text = plural(s.hmi.plcReads, "variable \xC3\xA0 lire", "variables \xC3\xA0 lire");
        b.why = "l'IHM ne tourne pas";
    } else if (p.prepared && p.state == State::Halted) {
        b.broken = true;
        b.text = reads;
        b.why = "l'automate est arr\xC3\xAAt\xC3\xA9 : l'IHM lit des valeurs fig\xC3\xA9" "es";
    } else if (p.prepared && p.state == State::Running) {
        b.alive = true;
        b.text = reads;
    } else {
        b.idle = true;
        b.text = reads;
        b.why = p.prepared && p.state == State::Paused ? "l'automate est en pause : les valeurs ne bougent pas"
                                                       : "l'automate est arr\xC3\xAAt\xC3\xA9 : les valeurs ne bougent pas";
    }
    return out;
}

std::vector<Attention> attention(const Snapshot& s) {
    std::vector<Attention> out;
    const auto& p = s.plc;
    const auto add = [&out](std::string id, Tone tone, int weight, std::string text, std::string detail, Action action) {
        Attention a;
        a.id = std::move(id);
        a.tone = tone;
        a.weight = weight;
        a.text = std::move(text);
        a.detail = std::move(detail);
        a.action = std::move(action);
        out.push_back(std::move(a));
    };
    if (!s.project) return out;
    if (!p.prepared && !p.prepareError.empty())
        add("preparation", Tone::Error, 380, "La simulation ne se pr\xC3\xA9pare pas", p.prepareError, {"R\xC3\xA9" "essayer", "sim.run"});
    if (p.prepared && p.state == State::Halted) {
        std::string detail = place(p.haltSection, p.haltLine);
        if (detail.empty()) detail = "au cycle " + thousands(p.cycle);
        else detail += ", au cycle " + thousands(p.cycle);
        // La maquette : une ligne, deux boutons - la mise a jour de la bibliotheque
        // (quand une version plus recente existe) et Voir la ligne ; sinon Voir la
        // ligne et Relancer du cycle 0.
        const std::string lineLabel = p.haltLine ? "Voir la ligne " + std::to_string(p.haltLine) : std::string("Voir la ligne");
        const bool newer = !p.haltNewer.empty() && !p.haltBlock.empty();
        if (newer)
            detail += " \xC2\xB7 " + p.haltBlock + " a une version plus r\xC3\xA9" "cente en biblioth\xC3\xA8que (" + p.haltNewer + ") : elle corrige peut-\xC3\xAAtre ce d\xC3\xA9" "faut";
        add("halte", Tone::Error, 390, "L'automate est arr\xC3\xAAt\xC3\xA9 : " + (p.haltReason.empty() ? std::string("un d\xC3\xA9" "faut") : p.haltReason), detail,
            newer ? Action{"Mettre " + p.haltBlock + " \xC3\xA0 jour " + p.haltNewer, "bibliotheque"}
            : !p.haltSection.empty() ? Action{lineLabel, lineKey(p.haltSection, p.haltLine)} : Action{"Relancer du cycle 0", "relancer"});
        if (newer && !p.haltSection.empty()) out.back().second = {lineLabel, lineKey(p.haltSection, p.haltLine)};
        else if (!newer && !p.haltSection.empty()) out.back().second = {"Relancer du cycle 0", "relancer"};
    }
    for (const auto* one : silent(s.equip)) {
        // La maquette : Retablir la liaison, puis Ouvrir les equipements.
        add("equipement:" + one->name, Tone::Error, 350, one->name + " ne r\xC3\xA9pond pas",
            one->why.empty() ? (one->state.empty() ? std::string("aucune r\xC3\xA9ponse") : one->state) : one->why,
            {"R\xC3\xA9tablir la liaison", "reconnecter:" + one->name});
        out.back().second = {"Ouvrir les \xC3\xA9quipements", "equipements"};
    }
    if (p.online && p.onlineFailed)
        add("en-ligne", Tone::Error, 340, "La modification en ligne n'est pas pass\xC3\xA9" "e", p.onlineSummary, {"Voir le journal", "journal"});
    if (s.hmi.running && s.hmi.scriptErrors)
        add("scripts", Tone::Warning, 260,
            s.hmi.scriptErrors == 1 ? std::string("Un script de l'IHM s'est arr\xC3\xAAt\xC3\xA9 sur une erreur")
                                    : std::to_string(s.hmi.scriptErrors) + " erreurs de scripts dans l'IHM",
            s.hmi.lastScriptError, {"Voir le journal", "journal"});
    if (p.prepared && !p.unknown.empty()) {
        const auto& first = p.unknown.front();
        std::string detail = "Premier appel : " + place(first.section, first.line);
        if (first.section.empty()) detail = "Appel\xC3\xA9" "es";
        detail += " \xC2\xB7 " + plural(static_cast<std::size_t>(unknownCalls(p)), "appel", "appels");
        detail += p.continueUnknown ? " \xC2\xB7 elles rendent 0 et le cycle continue" : " \xC2\xB7 le cycle s'arr\xC3\xAAte dessus";
        add("inconnues", p.continueUnknown ? Tone::Warning : Tone::Error, p.continueUnknown ? 250 : 330,
            (p.unknown.size() == 1 ? p.unknown.front().name + " n'est pas simul\xC3\xA9" "e" : std::to_string(p.unknown.size()) + " fonctions ne sont pas simul\xC3\xA9" "es : "
                                                                                      + unknownNames(p, 3)),
            detail, !first.section.empty() ? Action{"Voir", lineKey(first.section, first.line)} : Action{"Voir", "automate"});
    }
    if (s.hmi.running && s.hmi.unacked) {
        add("alarmes", s.hmi.topPriority == 1 ? Tone::Error : Tone::Warning, s.hmi.topPriority == 1 ? 300 : 240,
            s.hmi.unacked == 1 ? std::string("Une alarme \xC3\xA0 acquitter dans l'IHM") : std::to_string(s.hmi.unacked) + " alarmes \xC3\xA0 acquitter dans l'IHM",
            s.hmi.topAlarm.empty() ? std::string{} : "La plus prioritaire : " + s.hmi.topAlarm, {"Voir les alarmes", "alarmes"});
    } else if (s.hmi.running && s.hmi.activeAlarms) {
        add("alarmes", Tone::Info, 140,
            s.hmi.activeAlarms == 1 ? std::string("Une alarme active (acquitt\xC3\xA9" "e)") : std::to_string(s.hmi.activeAlarms) + " alarmes actives (acquitt\xC3\xA9" "es)",
            s.hmi.topAlarm, {"Voir les alarmes", "alarmes"});
    }
    if (p.forced) {
        const bool old = p.oldestForcedSeconds >= 300.0;
        add("forcages", old ? Tone::Warning : Tone::Info, old ? 220 : 120,
            (p.forced == 1 ? std::string("Une variable forc\xC3\xA9" "e") : std::to_string(p.forced) + " variables forc\xC3\xA9" "es")
                + (p.oldestForced.empty() ? std::string{} : ", depuis " + duration(p.oldestForcedSeconds) + " pour " + p.oldestForced),
            old ? "Un for\xC3\xA7" "age oubli\xC3\xA9 fausse la suite des essais : rel\xC3\xA2" "che-le quand tu as fini."
                : "Le programme n'\xC3\xA9" "crit plus dans une variable forc\xC3\xA9" "e : elle relit sa valeur forc\xC3\xA9" "e.",
            {"Voir les for\xC3\xA7" "ages", "forcages"});
        out.back().second = {"Tout rel\xC3\xA2" "cher", "forcages:tout-relacher"};   // la maquette : deux boutons
    }
    if (p.prepared && p.state == State::Running && p.cycle > 0 && p.scanMicros > 0) {
        const double ms = cycleMs(p);
        const double period = static_cast<double>(std::max<std::int64_t>(1, p.periodMs));
        if (ms > period) {
            const double ratio = ms / period;
            std::string detail = ratio >= 1.95 ? "Le temps simul\xC3\xA9 avance " + decimal1(std::round(ratio * 10.0) / 10.0) + " fois moins vite que l'horloge"
                                               : "Le temps simul\xC3\xA9 avance un peu moins vite que l'horloge";
            if (!p.heaviestSection.empty())
                detail += " \xC2\xB7 " + p.heaviestSection + " en prend " + milliseconds(static_cast<double>(p.heaviestMicros) / 1000.0);
            add("cycle-lent", ratio >= 2.0 ? Tone::Warning : Tone::Info, ratio >= 2.0 ? 210 : 110,
                "Un cycle se calcule en " + milliseconds(ms) + " pour " + std::to_string(p.periodMs) + " ms de p\xC3\xA9riode", detail,
                {"Voir les temps", "debogage"});
        }
    }
    // 1.10.2 : des sections qui ne tournent pas - leur condition d'activation est
    // fausse (comme sur l'automate) ; dit, pour qu'on ne cherche pas un defaut.
    if (p.prepared && p.inactiveSections > 0)
        add("sections-inactives", Tone::Info, 100,
            p.inactiveSections == 1 ? std::string("Une section ne tourne pas : sa condition d'activation est fausse")
                                    : std::to_string(p.inactiveSections) + " sections ne tournent pas : leur condition d'activation est fausse",
            p.inactiveSection + (p.inactiveCondition.empty() ? std::string{} : " (si " + p.inactiveCondition + ")")
                + (p.inactiveSections > 1 ? std::string(", et d'autres") : std::string{})
                + ". Elle reprend quand la condition devient vraie.",
            {"Voir les temps", "debogage"});
    if (p.prepared && p.breakHit)
        add("point-arret", Tone::Info, 170, "En pause sur un point d'arr\xC3\xAAt : " + place(p.breakSection, static_cast<std::uint32_t>(std::max(0, p.breakLine))),
            "Continuer reprend jusqu'au prochain point d'arr\xC3\xAAt.", {"Ouvrir le d\xC3\xA9" "bogage", "debogage"});
    if (p.prepared && p.stale)
        add("ancien-code", Tone::Info, 160, "Le programme a chang\xC3\xA9 depuis le lancement",
            "La simulation tourne sur l'ancien code jusqu'\xC3\xA0 Arr\xC3\xAAter ; Relancer prend le nouveau.", {"Relancer", "relancer"});
    if (p.online && !p.onlineFailed && p.onlineAgeSeconds < 60.0)
        add("en-ligne", Tone::Info, 130, "Modification en ligne faite", p.onlineSummary, {"Voir le journal", "journal"});
    if (s.hmi.exists && !s.hmi.running && p.prepared && p.state == State::Running)
        add("ihm-arretee", Tone::Info, 90, "L'IHM ne tourne pas", "L'automate tourne seul : lance l'IHM pour voir ses vues vivre.",
            {"Lancer l'IHM", "ihm"});
    std::stable_sort(out.begin(), out.end(), [](const Attention& x, const Attention& y) { return x.weight > y.weight; });
    if (out.size() > 8) out.resize(8);
    return out;
}

} // namespace

// ------------------------------------------------------------------- les mots ----
std::string thousands(std::uint64_t n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string milliseconds(double ms) {
    if (ms < 0) ms = 0;
    if (ms >= 1000.0) return decimal1(std::round(ms / 100.0) / 10.0) + " s";
    if (ms >= 10.0) return std::to_string(static_cast<long long>(std::lround(ms))) + " ms";
    return decimal1(std::round(ms * 10.0) / 10.0) + " ms";
}

std::string duration(double seconds) {
    if (seconds < 0) seconds = 0;
    const auto total = static_cast<long long>(std::floor(seconds + 0.5));
    if (total < 60) {
        if (seconds < 10.0 && seconds > 0.0 && std::fabs(seconds - std::round(seconds)) > 0.05) return decimal1(std::round(seconds * 10.0) / 10.0) + " s";
        return std::to_string(total) + " s";
    }
    if (total < 3600) {
        const auto m = total / 60, s = total % 60;
        return std::to_string(m) + " min" + (s ? " " + std::to_string(s) + " s" : std::string{});
    }
    const auto h = total / 3600, m = (total % 3600) / 60;
    return std::to_string(h) + " h" + (m ? " " + std::to_string(m) + " min" : std::string{});
}

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return thousands(n) + " " + (n == 1 ? one : many);
}

std::string toneName(Tone t) {
    switch (t) {
        case Tone::Ok:      return "vert";
        case Tone::Warning: return "orange";
        case Tone::Error:   return "rouge";
        case Tone::Info:    return "bleu";
        case Tone::Off:     break;
    }
    return "gris";
}

std::string bannerLine(const Report& r) {
    std::string out = "[" + toneName(r.banner.tone) + "] " + r.banner.title + " | Ce que \xC3\xA7" "a veut dire : " + r.banner.meaning;
    if (!r.banner.fix.empty()) out += " | Bouton : " + r.banner.fix.label + " (" + r.banner.fix.key + ")";
    if (!r.banner.second.empty()) out += " | " + r.banner.second.label + " (" + r.banner.second.key + ")";
    return out;
}

// ------------------------------------------------------------------- le calcul ----
Report compute(const Snapshot& s) {
    Report r;
    r.banner = banner(s);
    r.cards[0] = plcCard(s);
    r.cards[1] = hmiCard(s);
    r.cards[2] = equipCard(s);
    r.chain = chain(s);
    r.attention = attention(s);
    const auto& p = s.plc;
    if (!s.project) {
        r.folderTone = Tone::Off;
    } else if (p.prepared && p.state == State::Running) {
        r.folderBadge = "en marche";
        r.folderTone = Tone::Ok;
    } else if (p.prepared && p.state == State::Paused) {
        // Lot API 8 : corrections des captures - "arret" se lisait "arrete" (l'onglet
        // Automate dit "en pause") : "point d'arret" en orange, "pause" en bleu.
        r.folderBadge = p.breakHit ? "point d'arr\xC3\xAAt" : "pause";
        r.folderTone = p.breakHit ? Tone::Warning : Tone::Info;   // la maquette : bleu en pause, orange a l'arret
    } else if ((p.prepared && p.state == State::Halted) || (!p.prepared && !p.prepareError.empty())) {
        r.folderBadge = "d\xC3\xA9" "faut";
        r.folderTone = Tone::Error;
    } else {
        r.folderBadge = "arr\xC3\xAAt\xC3\xA9" "e";
        r.folderTone = Tone::Off;
    }
    if (s.hmi.running) r.hmiBadge = s.hmi.view.empty() ? std::string("en marche") : s.hmi.view;   // la maquette : la vue affichee
    const auto all = enabledCount(s.equip);
    const auto mute = silent(s.equip).size();
    if (mute) {
        r.equipBadge = plural(mute, "coup\xC3\xA9", "coup\xC3\xA9s");        // la maquette : "1 coupe"
        r.equipTone = Tone::Error;
    } else if (all) {
        r.equipBadge = simulatedCount(s.equip) == all ? plural(all, "simul\xC3\xA9", "simul\xC3\xA9s") : std::to_string(all);   // "4 simules"
        r.equipTone = Tone::Off;
    }
    return r;
}

} // namespace app::simstatus
