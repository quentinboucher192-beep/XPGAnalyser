// =============================================================================
//  app/SimJournal.cpp - lot API 8 : le journal de la simulation
// =============================================================================
#include "SimJournal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace app {

namespace {

// Sans la casse ni les accents (les lettres du francais, en UTF-8) : << Equipements >>
// trouve << Equipements >>.
std::string fold(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            char base = 0;
            if ((d >= 0x80 && d <= 0x86) || (d >= 0xA0 && d <= 0xA6)) base = 'a';
            else if (d == 0x87 || d == 0xA7) base = 'c';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'e';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'i';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'o';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'u';
            if (base) {
                out += base;
                ++i;
                continue;
            }
        }
        out += c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    }
    return out;
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string localClock() {
    const auto t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string grouped(std::uint64_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

// Une case CSV : entre guillemets si elle porte un ; ou un guillemet.
std::string cell(const std::string& s) {
    if (s.find_first_of(";\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += '"';
        out += c == '\n' ? ' ' : c;
    }
    return out + "\"";
}

} // namespace

SimJournal::SimJournal() : clock_(steadySeconds), wall_(localClock) {}

void SimJournal::setClock(std::function<double()> clock, std::function<std::string()> wallClock) {
    clock_ = clock ? std::move(clock) : std::function<double()>(steadySeconds);
    wall_ = wallClock ? std::move(wallClock) : std::function<std::string()>(localClock);
}

double SimJournal::now() const { return clock_ ? clock_() : steadySeconds(); }

void SimJournal::setCapacity(std::size_t n) {
    capacity_ = std::max<std::size_t>(n, 16);
    while (events_.size() > capacity_) events_.pop_front();
    ++revision_;
}

std::uint64_t SimJournal::add(SimSource source, SimSeverity severity, std::string text, std::string go) {
    return add(source, severity, source == SimSource::Debogage ? "debogage" : "info", std::move(text), {}, std::move(go));
}

std::uint64_t SimJournal::add(SimSource source, SimSeverity severity, std::string kind, std::string text, std::string explain,
                              std::string go) {
    const double t = now();
    // Un arret au meme endroit, dit deux fois (le collecteur par breakHit, le
    // debogage par sa phrase) : une seule ligne, la derniere phrase.
    if (!events_.empty() && source == SimSource::Debogage && (kind == "arret" || kind == "point-arret")) {
        auto& last = events_.back();
        if (last.source == SimSource::Debogage && (last.kind == "arret" || last.kind == "point-arret") && last.go == go && t - last.time < 2.0) {
            last.kind = std::move(kind);
            last.text = std::move(text);
            if (!explain.empty()) last.explain = std::move(explain);
            last.severity = severity;
            ++revision_;
            return last.id;
        }
    }
    // Le meme, aussitot : il se compte au lieu de se repeter (une liaison qui
    // clignote, une touche tenue).
    if (!events_.empty()) {
        auto& last = events_.back();
        if (last.source == source && last.severity == severity && last.kind == kind && last.text == text && t - last.time < 2.0) {
            ++last.repeats;
            last.time = t;
            last.cycle = cycle_;
            if (wall_) last.clock = wall_();
            ++revision_;
            return last.id;
        }
    }
    SimEvent e;
    e.id = nextId_++;
    e.time = t;
    e.clock = wall_ ? wall_() : std::string{};
    e.cycle = cycle_;
    e.source = source;
    e.severity = severity;
    e.kind = std::move(kind);
    e.text = std::move(text);
    e.explain = std::move(explain);
    e.go = std::move(go);
    events_.push_back(std::move(e));
    while (events_.size() > capacity_) events_.pop_front();
    ++revision_;
    return events_.back().id;
}

void SimJournal::clear() {
    events_.clear();
    ++revision_;
}

const SimEvent* SimJournal::find(std::uint64_t id) const {
    for (const auto& e : events_)
        if (e.id == id) return &e;
    return nullptr;
}

std::vector<const SimEvent*> SimJournal::since(double seconds) const {
    std::vector<const SimEvent*> out;
    const double from = now() - seconds;
    for (auto it = events_.rbegin(); it != events_.rend() && it->time >= from; ++it) out.push_back(&*it);
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<const SimEvent*> SimJournal::filtered(unsigned sources, SimSeverity atLeast, std::string_view search) const {
    std::vector<const SimEvent*> out;
    const std::string want = fold(search);
    const auto rank = [](SimSeverity s) {
        // Ok compte comme Info : << a surveiller >> ne montre pas ce qui repart.
        return s == SimSeverity::Error ? 2 : s == SimSeverity::Warning ? 1 : 0;
    };
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        const auto& e = *it;
        if (sources && !(sources & bit(e.source))) continue;
        if (rank(e.severity) < rank(atLeast)) continue;
        if (!want.empty() && fold(e.text).find(want) == std::string::npos && fold(e.kind).find(want) == std::string::npos
            && fold(explanationOf(e)).find(want) == std::string::npos && fold(sourceName(e.source)).find(want) == std::string::npos)
            continue;
        out.push_back(&e);
    }
    return out;
}

std::size_t SimJournal::count(SimSource source) const {
    return static_cast<std::size_t>(std::count_if(events_.begin(), events_.end(), [source](const SimEvent& e) { return e.source == source; }));
}

std::size_t SimJournal::countAtLeast(SimSeverity severity) const {
    const auto rank = [](SimSeverity s) { return s == SimSeverity::Error ? 2 : s == SimSeverity::Warning ? 1 : 0; };
    return static_cast<std::size_t>(
        std::count_if(events_.begin(), events_.end(), [&](const SimEvent& e) { return rank(e.severity) >= rank(severity); }));
}

std::string SimJournal::sourceName(SimSource s) {
    switch (s) {
        case SimSource::Automate:    return "Automate";
        case SimSource::Ihm:         return "IHM";
        case SimSource::Equipements: return "\xC3\x89quipements";
        case SimSource::Debogage:    return "D\xC3\xA9" "bogage";
        case SimSource::Simulation:  break;
    }
    return "Simulation";
}

std::string SimJournal::severityName(SimSeverity s) {
    switch (s) {
        case SimSeverity::Ok:      return "ok";
        case SimSeverity::Warning: return "\xC3\xA0 surveiller";
        case SimSeverity::Error:   return "erreur";
        case SimSeverity::Info:    break;
    }
    return "info";
}

std::string SimJournal::defaultExplanation(std::string_view kind, SimSource source, SimSeverity severity) {
    if (kind == "demarrage")
        return "Le programme s'ex\xC3\xA9" "cute cycle apr\xC3\xA8s cycle : les valeurs changent, tu les lis au survol, dans les tables et les courbes.";
    if (kind == "reprise") return "Le programme repart d'o\xC3\xB9 il s'\xC3\xA9tait arr\xC3\xAAt\xC3\xA9 : les valeurs n'ont pas \xC3\xA9t\xC3\xA9 remises \xC3\xA0 z\xC3\xA9ro.";
    if (kind == "pause") return "Plus rien n'avance : les valeurs restent fig\xC3\xA9" "es. Un cycle avance d'un pas, Simuler reprend.";
    if (kind == "arret")
        return "Tout revient \xC3\xA0 z\xC3\xA9ro : chaque variable et chaque temporisation repart de sa valeur initiale au prochain lancement.";
    if (kind == "cycle") return "Un seul cycle a tourn\xC3\xA9, puis la simulation s'est remise en pause : de quoi suivre pas \xC3\xA0 pas.";
    if (kind == "halte")
        return "Un d\xC3\xA9" "faut a arr\xC3\xAAt\xC3\xA9 le cycle : rien ne bouge plus, les sorties gardent leur derni\xC3\xA8re valeur. Corrige, puis Arr\xC3\xAAter "
               "et Simuler : le programme repart de z\xC3\xA9ro.";
    if (kind == "preparation")
        return severity == SimSeverity::Error ? "Le simulateur n'a pas pu lire le programme : rien ne peut tourner tant que ce n'est pas corrig\xC3\xA9."
                                              : "Le simulateur a lu le programme de MAST : il est pr\xC3\xAAt \xC3\xA0 tourner.";
    if (kind == "forcage")
        return "Le programme n'\xC3\xA9" "crit plus dans cette variable : elle relit toujours la valeur forc\xC3\xA9" "e, jusqu'\xC3\xA0 ce que tu la rel\xC3\xA2" "ches.";
    if (kind == "relache") return "Le programme \xC3\xA9" "crit de nouveau dans cette variable.";
    if (kind == "inconnue")
        return "Le simulateur ne conna\xC3\xAEt pas cette fonction : elle rend 0 et le cycle continue, mais ce qu'elle devait calculer est faux.";
    if (kind == "en-ligne")
        return severity == SimSeverity::Error
                   ? "Le programme modifi\xC3\xA9 n'a pas pu remplacer celui qui tourne : la simulation continue sur l'ancien code."
                   : "Le programme a chang\xC3\xA9 sans arr\xC3\xAAter la simulation : les valeurs sont gard\xC3\xA9" "es, le nouveau code tourne d\xC3\xA8s le cycle suivant.";
    if (kind == "point-arret" || kind == "arret")
        return "La simulation s'est mise en pause sur un point d'arr\xC3\xAAt : tu peux lire les valeurs de la ligne, puis continuer.";
    if (kind == "ihm-demarrage") return "L'IHM tourne sur l'automate simul\xC3\xA9 : ses vues lisent et \xC3\xA9" "crivent ses variables.";
    if (kind == "ihm-arret") return "L'IHM ne tourne plus : ses vues ne lisent plus l'automate.";
    if (kind == "vue") return "L'IHM affiche cette vue : c'est ce que verrait l'op\xC3\xA9rateur.";
    if (kind == "alarme")
        return "Une alarme de l'IHM est apparue : sa condition est vraie. Elle reste dans la liste tant qu'elle est active ou pas acquitt\xC3\xA9" "e.";
    if (kind == "alarme-fin") return "La condition de l'alarme est redevenue fausse.";
    if (kind == "utilisateur") return "L'utilisateur connect\xC3\xA9 \xC3\xA0 l'IHM a chang\xC3\xA9 : ce qu'il peut faire d\xC3\xA9pend de son niveau.";
    if (kind == "script") return "Un script de l'IHM s'est arr\xC3\xAAt\xC3\xA9 sur une erreur : ce qu'il devait faire n'a pas \xC3\xA9t\xC3\xA9 fait.";
    if (kind == "liaison")
        return severity == SimSeverity::Error ? "L'IHM ne parle plus \xC3\xA0 cet \xC3\xA9quipement : ses variables deviennent mauvaises (croix rouge)."
                                              : "L'\xC3\xA9quipement r\xC3\xA9pond de nouveau : ses variables se relisent.";
    if (kind == "equipements") return "Les \xC3\xA9quipements de l'IHM ont chang\xC3\xA9 : leurs liaisons se refont.";
    if (kind == "programme")
        return "Le programme a chang\xC3\xA9 depuis le lancement : la simulation tourne encore sur l'ancien code jusqu'\xC3\xA0 Arr\xC3\xAAter.";
    if (kind == "politique") return "Ce que fait le simulateur devant une fonction qu'il ne conna\xC3\xAEt pas.";
    if (kind == "vitesse") return "La vitesse de la simulation : x1 suit l'horloge, x10 va dix fois plus vite, au plus vite encha\xC3\xAEne les cycles.";
    if (source == SimSource::Debogage) return "Un \xC3\xA9v\xC3\xA9nement du d\xC3\xA9" "bogage.";
    switch (severity) {
        case SimSeverity::Error:   return "Quelque chose s'est arr\xC3\xAAt\xC3\xA9 ou coup\xC3\xA9 : regarde ce que dit la ligne.";
        case SimSeverity::Warning: return "\xC3\x80 surveiller : la simulation continue, mais pas tout \xC3\xA0 fait comme tu l'attends.";
        case SimSeverity::Ok:      return "Ce qui s'\xC3\xA9tait arr\xC3\xAAt\xC3\xA9 est reparti.";
        case SimSeverity::Info:    break;
    }
    return "Pour m\xC3\xA9moire.";
}

std::string SimJournal::explanationOf(const SimEvent& e) {
    return e.explain.empty() ? defaultExplanation(e.kind, e.source, e.severity) : e.explain;
}

std::string SimJournal::goLine(const std::string& section, int line) {
    return "ligne:" + section + ":" + std::to_string(line < 0 ? 0 : line);
}

std::string SimJournal::line(const SimEvent& e) {
    std::string out = e.clock + "  cycle " + grouped(e.cycle) + "  " + sourceName(e.source) + "  " + severityName(e.severity) + "  " + e.text;
    if (e.repeats > 1) out += "  (x " + std::to_string(e.repeats) + ")";
    return out;
}

std::string SimJournal::csv() const {
    std::string out = "Heure;Cycle;Source;Gravit\xC3\xA9;Genre;\xC3\x89v\xC3\xA9nement;Ce que \xC3\xA7" "a veut dire;R\xC3\xA9p\xC3\xA9t\xC3\xA9\r\n";
    for (const auto& e : events_)
        out += cell(e.clock) + ";" + std::to_string(e.cycle) + ";" + cell(sourceName(e.source)) + ";" + cell(severityName(e.severity)) + ";"
             + cell(e.kind) + ";" + cell(e.text) + ";" + cell(explanationOf(e)) + ";" + std::to_string(e.repeats) + "\r\n";
    return out;
}

} // namespace app
