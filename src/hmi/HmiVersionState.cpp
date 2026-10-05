// hmi/HmiVersionState.cpp - l'etat du projet et les versions (lot API 6).
#include "HmiVersionState.hpp"

#include <cctype>
#include <ctime>

namespace hmi::ver {

namespace {

std::string lowerFirst(std::string_view s) {
    std::string out(s);
    if (!out.empty() && out[0] >= 'A' && out[0] <= 'Z') out[0] = static_cast<char>(out[0] - 'A' + 'a');
    return out;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string quoted(const Version& v) { return v.name.empty() ? std::string("sans nom") : "\xC2\xAB " + v.name + " \xC2\xBB"; }

} // namespace

std::string whenText(const std::string& stamp) {
    if (stamp.size() < 16) return stamp;
    return stamp.substr(8, 2) + "/" + stamp.substr(5, 2) + " \xC3\xA0 " + stamp.substr(11, 5);
}

Standing standing(project::State state, const Store* store, std::size_t changes, std::size_t unsaved) {
    Standing s;
    s.state = state;
    const Version* last = store ? store->last() : nullptr;
    s.last = last ? last->number : 0;
    const std::string lastLine = last ? "V" + std::to_string(last->number) + " \xC2\xB7 " + quoted(*last) + ", " + lowerFirst(stateLabel(last->state))
                                            + " le " + whenText(last->date)
                                      : std::string{};
    switch (state) {
        case project::State::New:
            s.current = s.last + 1;
            s.tone = "new";
            s.title = "V" + std::to_string(s.current) + " \xC3\xA0 venir";
            s.subtitle = last ? "depuis V" + std::to_string(s.last) : std::string("aucune version encore");
            s.heading = {"Aucune modification encore", "La premi\xC3\xA8re modification commence la V" + std::to_string(s.current) + " (DEV)."};
            s.tip = "NEW : le projet vient d'\xC3\xAAtre cr\xC3\xA9\xC3\xA9 ; la premi\xC3\xA8re modification commence la V" + std::to_string(s.current) + ".";
            break;
        case project::State::Dev: {
            s.current = s.last + 1;
            s.tone = "dev";
            s.title = "V" + std::to_string(s.current) + " en cours";
            s.subtitle = last ? "depuis V" + std::to_string(s.last) + " \xC2\xB7 " + lowerFirst(stateLabel(last->state)) : std::string("la premi\xC3\xA8re version");
            s.heading.push_back("Tu modifies la V" + std::to_string(s.current));
            s.heading.push_back(last ? "partie de la " + lastLine : std::string("la premi\xC3\xA8re version du projet"));
            std::string counts = last ? plural(changes, "\xC3\xA9l\xC3\xA9ment chang\xC3\xA9", "\xC3\xA9l\xC3\xA9ments chang\xC3\xA9s") + " depuis la V" + std::to_string(s.last)
                                      : std::string("pas encore de version pour comparer");
            if (unsaved > 0) counts += " \xC2\xB7 " + plural(unsaved, "modification non enregistr\xC3\xA9" "e", "modifications non enregistr\xC3\xA9" "es");
            s.heading.push_back(counts);
            s.tip = "DEV : tu modifies la V" + std::to_string(s.current) + (last ? ", partie de la V" + std::to_string(s.last) : std::string{})
                  + ". Terminer en fait une version valid\xC3\xA9" "e (FINISH).";
            break;
        }
        case project::State::Finish:
            s.current = s.last;
            s.tone = "finish";
            // Un FINISH d'avant le lot API 6 : sa derniere version n'est pas une
            // version terminee (un brouillon, une automatique) - on le dit tel quel.
            if (last && last->state != State::Validated && last->state != State::Delivered) {
                s.title = "FINISH";
                s.subtitle = "derni\xC3\xA8re version : V" + std::to_string(s.last);
                s.heading = {"Le projet est FINISH", "sa derni\xC3\xA8re version, " + lastLine + ", n'est pas une version termin\xC3\xA9" "e",
                             "La premi\xC3\xA8re modification commencera la V" + std::to_string(s.last + 1) + ", sans rien demander."};
                s.tip = "FINISH : modifier commence la V" + std::to_string(s.last + 1) + ".";
            } else if (last) {
                s.title = "V" + std::to_string(s.last) + " " + lowerFirst(stateLabel(last->state == State::Delivered ? State::Delivered : State::Validated));
                s.subtitle = "termin\xC3\xA9" "e le " + whenText(last->date);
                s.heading = {"La V" + std::to_string(s.last) + " est termin\xC3\xA9" "e", quoted(*last) + " \xC2\xB7 le projet est exactement la V" + std::to_string(s.last),
                             "La premi\xC3\xA8re modification commencera la V" + std::to_string(s.last + 1) + ", sans rien demander."};
                s.tip = "FINISH : le projet est la V" + std::to_string(s.last) + " ; modifier commence la V" + std::to_string(s.last + 1) + ".";
            } else {
                s.title = "FINISH";
                s.subtitle = "sans version";
                s.heading = {"Le projet est FINISH, sans version", "Livrer et verrouiller cr\xC3\xA9" "e la V1 ; modifier commence la V1."};
                s.tip = "FINISH d'avant les versions : aucune version ne le fige encore.";
            }
            break;
        case project::State::Lock:
            s.current = s.last;
            s.tone = "lock";
            if (last) {
                s.title = "V" + std::to_string(s.last) + (last->state == State::Delivered ? " livr\xC3\xA9" "e" : " verrouill\xC3\xA9" "e");
                s.subtitle = "verrouill\xC3\xA9" "e \xC2\xB7 lecture seule";
                s.heading = {"La V" + std::to_string(s.last) + " est livr\xC3\xA9" "e et verrouill\xC3\xA9" "e", "personne ne la modifie \xC2\xB7 " + lastLine,
                             "D\xC3\xA9verrouiller (mot de passe ou PC ma\xC3\xAEtre) commence la V" + std::to_string(s.last + 1) + "."};
            } else {
                s.title = "LOCK";
                s.subtitle = "verrouill\xC3\xA9 \xC2\xB7 lecture seule";
                s.heading = {"Le projet est verrouill\xC3\xA9", "D\xC3\xA9verrouiller (mot de passe ou PC ma\xC3\xAEtre) : il repasse en DEV."};
            }
            s.tip = "LOCK : personne ne modifie le projet. Projet > D\xC3\xA9verrouiller.";
            break;
    }
    return s;
}

Closing closing(project::State from, project::State to, const Store& store, bool changedSinceLast) {
    Closing c;
    const Version* last = store.last();
    if (to == project::State::Lock && from == project::State::Finish && last && !changedSinceLast) {
        c.promote = true;
        c.number = last->number;
        c.versionState = State::Delivered;
        return c;
    }
    c.number = store.nextNumber();
    c.versionState = to == project::State::Lock ? State::Delivered : State::Validated;
    return c;
}

std::string defaultName(project::State to, const std::string& now) {
    std::string stamp = now;
    if (stamp.size() < 10) {
        const std::time_t t = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
        stamp = buf;
    }
    const std::string day = stamp.substr(8, 2) + "/" + stamp.substr(5, 2);
    return (to == project::State::Lock ? "Livr\xC3\xA9" "e le " : "Termin\xC3\xA9" "e le ") + day;
}

} // namespace hmi::ver
