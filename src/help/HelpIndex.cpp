// =============================================================================
//  help/HelpIndex.cpp
// =============================================================================
#include "HelpIndex.hpp"

#include "HelpCodes.hpp"

#include <algorithm>
#include <cctype>

namespace help {
namespace {

// Les accents tombent, la casse aussi. Personne ne tape « periode » avec son
// accent dans une barre de recherche, et personne ne devrait avoir a le faire.
std::string fold(std::string_view s) {
    static const struct { const char* from; char to; } kAccents[] = {
        {"\xC3\xA0", 'a'}, {"\xC3\xA2", 'a'}, {"\xC3\xA4", 'a'},
        {"\xC3\xA7", 'c'},
        {"\xC3\xA9", 'e'}, {"\xC3\xA8", 'e'}, {"\xC3\xAA", 'e'}, {"\xC3\xAB", 'e'},
        {"\xC3\xAE", 'i'}, {"\xC3\xAF", 'i'},
        {"\xC3\xB4", 'o'}, {"\xC3\xB6", 'o'},
        {"\xC3\xB9", 'u'}, {"\xC3\xBB", 'u'}, {"\xC3\xBC", 'u'},
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        bool done = false;
        if (static_cast<unsigned char>(s[i]) >= 0x80) {
            for (const auto& a : kAccents) {
                const std::string_view from(a.from);
                if (s.compare(i, from.size(), from) == 0) {
                    out.push_back(a.to);
                    i += from.size();
                    done = true;
                    break;
                }
            }
            if (!done) { out.push_back(s[i]); ++i; }
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
        ++i;
    }
    return out;
}

bool contains(std::string_view hay, std::string_view needle) {
    return fold(hay).find(fold(needle)) != std::string::npos;
}

// Le passage autour du terme, borne aux mots. Un extrait coupe au milieu d'un
// mot se lit deux fois.
std::string excerptAround(std::string_view text, std::string_view term,
                          std::size_t width = 110) {
    const auto folded = fold(text);
    const auto at = folded.find(fold(term));
    if (at == std::string::npos) return std::string(text.substr(0, std::min(text.size(), width)));

    std::size_t begin = at > width / 2 ? at - width / 2 : 0;
    while (begin > 0 && text[begin] != ' ') --begin;
    std::size_t end = std::min(text.size(), at + width / 2);
    while (end < text.size() && text[end] != ' ') ++end;

    std::string out;
    if (begin > 0) out += "...";
    out += std::string(text.substr(begin, end - begin));
    if (end < text.size()) out += "...";
    // Une aide est multiligne ; un resultat de recherche tient sur une ligne.
    for (auto& c : out) if (c == '\n') c = ' ';
    return out;
}

} // namespace

// =============================================================== recherche ===
std::vector<Hit> search(const std::vector<project::CatalogEntry>& library,
                        std::string_view term, std::size_t limit) {
    std::vector<Hit> hits;
    if (fold(term).empty()) return hits;
    const auto t = fold(term);

    for (const auto& e : library) {
        const auto nom = fold(e.name);
        // LE CLASSEMENT EST ICI, et c'est tout le sujet. Le nom exact passe
        // devant un nom qui contient, qui passe devant un parametre, qui passe
        // devant une phrase. Sans ca, chercher « Val » rend d'abord les
        // quarante phrases qui contiennent « valeur ».
        if (nom == t)
            hits.push_back({HitKind::Name, e.name, {}, e.help.summary, 1000});
        else if (nom.find(t) != std::string::npos)
            hits.push_back({HitKind::Name, e.name, {}, e.help.summary, 800});

        for (const auto& d : e.declarations) {
            if (d.name.empty() || d.isLocal()) continue;
            const auto p = fold(d.name);
            if (p == t)
                hits.push_back({HitKind::Param, e.name, d.name,
                                d.comment.empty() ? d.type : d.type + "  -  " + d.comment, 700});
            else if (p.find(t) != std::string::npos)
                hits.push_back({HitKind::Param, e.name, d.name, d.type, 500});
        }
        for (const auto& f : e.faultTable)
            if (fold(f.code) == t || contains(f.text, term))
                hits.push_back({HitKind::Fault, e.name, f.code, f.text, 450});

        if (contains(e.help.summary, term))
            hits.push_back({HitKind::Summary, e.name, {},
                            excerptAround(e.help.summary, term), 400});
        if (contains(e.help.usage, term))
            hits.push_back({HitKind::Usage, e.name, {},
                            excerptAround(e.help.usage, term), 250});
        if (contains(e.help.example, term))
            hits.push_back({HitKind::Example, e.name, {},
                            excerptAround(e.help.example, term), 200});
        for (const auto& p : e.help.params)
            if (contains(p.text, term))
                hits.push_back({HitKind::Param, e.name, p.key,
                                excerptAround(p.text, term), 300});
    }

    // Les codes de diagnostic : leur numero se tape entier, ou par famille.
    for (const auto& c : allCodes()) {
        if (contains(c.id, term))
            hits.push_back({HitKind::Diagnostic, std::string(c.id), {}, std::string(c.title), 900});
        else if (contains(c.title, term) || contains(c.cause, term) || contains(c.effect, term))
            hits.push_back({HitKind::Diagnostic, std::string(c.id), {},
                            excerptAround(c.title, term), 350});
    }

    for (const auto& g : glossary())
        if (fold(g.word) == t)
            hits.push_back({HitKind::Glossary, std::string(g.word), {},
                            std::string(g.short_), 950});
        else if (contains(g.word, term) || contains(g.short_, term))
            hits.push_back({HitKind::Glossary, std::string(g.word), {},
                            std::string(g.short_), 380});

    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (limit != 0 && hits.size() > limit) hits.resize(limit);
    return hits;
}

// ================================================================ renvois ====
std::vector<std::string> seeNames(const project::LibraryHelp& h) {
    std::vector<std::string> out;
    for (const auto& line : h.see) {
        std::size_t start = 0;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i != line.size() && line[i] != ',' && line[i] != ';') continue;
            std::size_t a = start, b = i;
            while (a < b && static_cast<unsigned char>(line[a]) <= ' ') ++a;
            while (b > a && static_cast<unsigned char>(line[b - 1]) <= ' ') --b;
            if (b > a) out.push_back(line.substr(a, b - a));
            start = i + 1;
        }
    }
    return out;
}

std::vector<Backlink> backlinks(const std::vector<project::CatalogEntry>& library,
                                std::string_view name) {
    std::vector<Backlink> out;
    for (const auto& e : library) {
        if (e.name == name) continue;
        for (const auto& s : seeNames(e.help))
            if (s == name) { out.push_back({e.name, "cite dans Voir aussi"}); break; }

        for (const auto& d : e.declarations) {
            if (d.type.find(name) == std::string::npos) continue;
            // « ARRAY[0..15] OF ST_IO_Dig » contient le nom ; un type qui
            // commence pareil - ST_IO_Dig2 - ne doit pas compter, d'ou le
            // controle des bornes du mot.
            const auto at = d.type.find(name);
            const auto after = at + name.size();
            const bool motEntier =
                (at == 0 || !std::isalnum(static_cast<unsigned char>(d.type[at - 1])))
                && (after >= d.type.size()
                    || (!std::isalnum(static_cast<unsigned char>(d.type[after]))
                        && d.type[after] != '_'));
            if (!motEntier) continue;
            out.push_back({e.name, "parametre " + d.name});
            break;
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Backlink& a, const Backlink& b) { return a.name < b.name; });
    return out;
}

// ============================================================== glossaire ====
const std::vector<Term>& glossary() {
    static const std::vector<Term> kTerms = {
{"TC", "Telecommande : l'IHM ecrit, l'automate lit.",
 "Une commande venant de la supervision. Elle se lit EN TETE de tache : lue en "
 "fin de cycle, elle fait travailler tout le programme sur la commande du cycle "
 "precedent, et un bouton repond avec un cycle de retard."},
{"TM", "Telemesure : l'automate ecrit, l'IHM lit.",
 "Une valeur publiee vers la supervision. Elle s'ecrit EN QUEUE de tache, apres "
 "que le programme l'ait calculee : ecrite en tete, elle publie la valeur du "
 "cycle precedent."},
{"TA", "Telealarme : un defaut publie vers l'IHM.",
 "Comme une TM, mais c'est un booleen de defaut. Depuis ImporterProgramme, une "
 "TA est aussi enregistree dans le registre de DFB_ALM_MANAGER : sans ca elle "
 "s'affiche mais ne se date pas, ne s'acquitte pas et n'entre pas dans "
 "l'historique."},
{"DDT", "Derived Data Type : une structure de donnees.",
 "Un type compose, declare une fois et instancie autant de fois qu'on veut. "
 "ST_EQ_Pump, ST_IO_Dig en sont. Un DDT ne contient QUE des donnees : pas de "
 "code, donc pas de temporisateur - c'est pourquoi les blocs recoivent CycleMs."},
{"DFB", "Derived Function Block : un bloc fonction.",
 "Du code avec une memoire. Chaque instance garde son etat d'un cycle sur "
 "l'autre, ce qu'une fonction ne fait pas. DFB_EQ_PUMP, DFB_IO_DIG16 en sont."},
{"POU", "Program Organisation Unit : ce qui contient du code.",
 "Une section, une unite de programme, un type de bloc, une sous-routine. Le "
 "modele les range tous au meme endroit, et c'est leur genre qui les separe."},
{"EBOOL", "Un BOOL qui garde ses fronts.",
 "Un booleen etendu : en plus de sa valeur, il retient le front montant et le "
 "front descendant du cycle. C'est ce qui permet a %M12 de servir de bit de "
 "front sans variable supplementaire. Un BOOL ordinaire n'a pas cette memoire."},
{"%MW", "Un mot de la memoire interne.",
 "Un entier 16 bits, adresse par son numero : %MW1000. C'est le vehicule "
 "habituel des reports vers l'IHM. %MW1000:X3 en designe le bit 3."},
{"%M", "Un bit de la memoire interne.",
 "Un booleen adresse par son numero : %M40. Ecrit %MX40 dans certaines "
 "notations. Les zones MW et MX sont declarees dans la configuration de "
 "l'automate et l'onglet Config du classeur."},
{"%I", "Une entree physique.",
 "%I0.4.2 : rack 0, module 4, voie 2. Une entree tout ou rien. %IW pour une "
 "entree analogique, %Q et %QW pour les sorties."},
{"MAST", "La tache principale.",
 "La tache cyclique ou periodique qui porte le programme. Control Expert "
 "execute ses sections DANS L'ORDRE DU NAVIGATEUR DE PROJET, et cet ordre se "
 "pose a la main : creer une section ne la place pas."},
{"watchdog", "Le chien de garde de la tache.",
 "La duree au-dela de laquelle l'automate declare la tache en defaut et "
 "s'arrete. C'est pourquoi une boucle sans fin n'est pas un ralentissement : "
 "c'est un arret."},
{"front montant", "Le passage de FALSE a TRUE, vu une seule fois.",
 "Un ordre pris sur front s'execute au changement, pas tant que le bit est a "
 "TRUE. Les commandes du gestionnaire d'alarmes - Ack, Reset, Register - le "
 "sont toutes : sans ca, maintenir le bit acquitterait en boucle."},
{"anti-rebond", "Le temps qu'un changement doit tenir pour etre cru.",
 "DebounceMs sur une voie tout ou rien. A zero, tout passe, ce qui est le cas "
 "habituel. Quelques dizaines de millisecondes suffisent a filtrer un contact "
 "sec qui grelotte."},
{"hysteresis", "L'ecart qui evite qu'un seuil ne clignote.",
 "Hyst sur un seuil analogique. Le seuil se leve a Val et ne retombe qu'a "
 "Val - Hyst : une mesure qui oscille autour de la consigne ne produit donc "
 "pas une alarme par seconde."},
{"UUID", "L'identifiant compose d'une alarme.",
 "32 bits : genre, rack, module, voie, sous-numero, code. Compose par AlmUuid, "
 "relu par AlmDecode, et cite par l'IHM. Il ne depend PAS de l'emplacement dans "
 "le registre, sans quoi inserer une alarme changerait toutes les vues."},
{"DDT membre", "Un champ d'une structure.",
 "Dans le modele, un membre de DDT est une variable de portee DerivedMember "
 "dont le proprietaire est le type. C'est ce qui permet a une meme structure de "
 "cinquante champs de ne couter qu'une declaration."},
    };
    return kTerms;
}

const Term* glossaryTerm(std::string_view word) {
    for (const auto& t : glossary())
        if (fold(t.word) == fold(word)) return &t;
    return nullptr;
}

std::vector<Mention> mentions(std::string_view text) {
    std::vector<Mention> out;
    const auto folded = fold(text);
    for (const auto& t : glossary()) {
        const auto needle = fold(t.word);
        if (needle.empty()) continue;
        std::size_t at = 0;
        while ((at = folded.find(needle, at)) != std::string::npos) {
            // Un mot entier, pas un morceau : « TM » ne doit pas s'allumer dans
            // « ATMOSPHERE ». Les symboles comme %MW n'ont pas cette contrainte
            // a gauche, puisqu'ils commencent par un caractere qui n'est pas
            // une lettre.
            const bool gaucheOk = at == 0
                || !(std::isalnum(static_cast<unsigned char>(folded[at - 1]))
                     || folded[at - 1] == '_');
            const auto after = at + needle.size();
            const bool droiteOk = after >= folded.size()
                || !(std::isalnum(static_cast<unsigned char>(folded[after]))
                     || folded[after] == '_');
            if (gaucheOk && droiteOk) out.push_back({at, needle.size(), &t});
            at = after;
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Mention& a, const Mention& b) { return a.at < b.at; });
    return out;
}

// ================================================================== situer ===
std::string wordAt(std::string_view line, std::size_t column) {
    if (line.empty()) return {};
    if (column >= line.size()) column = line.size() - 1;

    const auto partOfWord = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '%'
            || c == '.' || c == '[' || c == ']';
    };
    if (!partOfWord(line[column])) return {};

    std::size_t b = column, e = column;
    while (b > 0 && partOfWord(line[b - 1])) --b;
    while (e + 1 < line.size() && partOfWord(line[e + 1])) ++e;
    return std::string(line.substr(b, e - b + 1));
}

Target targetForWord(const std::vector<project::CatalogEntry>& library,
                     std::string_view word) {
    if (word.empty()) return {};

    // `Pompes[0].Fbk` designe `Fbk`. On coupe par la droite : c'est le dernier
    // segment qui porte le sens, et c'est celui sur lequel le curseur etait.
    std::string mot(word);
    if (const auto dot = mot.rfind('.'); dot != std::string::npos)
        mot = mot.substr(dot + 1);
    if (const auto br = mot.find('['); br != std::string::npos)
        mot = mot.substr(0, br);
    if (mot.empty()) return {};

    // L'ORDRE DES ESSAIS EST LE SUJET. `Val` est a la fois un parametre de trois
    // DDT et un mot courant ; `TM` est un mot du glossaire et rien d'autre. Un
    // nom d'element l'emporte sur un parametre, un parametre sur un mot.
    for (const auto& e : library)
        if (e.name == mot) return {TargetKind::LibraryEntry, e.name, {}};

    if (const auto* c = find(mot)) return {TargetKind::Diagnostic, std::string(c->id), {}};

    for (const auto& e : library)
        if (e.declaration(mot) != nullptr)
            return {TargetKind::Parameter, e.name, mot};

    if (glossaryTerm(mot) != nullptr) return {TargetKind::Glossary, mot, {}};
    if (glossaryTerm(word) != nullptr) return {TargetKind::Glossary, std::string(word), {}};
    return {};
}

Target targetForDiagnostic(std::string_view message) {
    const auto code = codeFor(message);
    if (code.empty()) return {};
    return {TargetKind::Diagnostic, std::string(code), {}};
}

// ================================================================ version ====
std::string versionNotice(std::string_view entryName, std::string_view libraryVersion,
                          std::string_view projectVersion) {
    if (libraryVersion.empty() || projectVersion.empty()) return {};
    if (libraryVersion == projectVersion) return {};

    std::string s = "Cette page decrit la " + std::string(libraryVersion)
                  + ". Ton projet a la " + std::string(projectVersion) + ".";

    // LE CAS QUI COMPTE VRAIMENT. La 1.01 corrige le bornage de Count : en 1.00
    // la derniere voie - ou le dernier equipement - n'est jamais traitee. Le
    // dire ici, dans l'article, plutot que dans une boite qu'on ferme.
    if (projectVersion == "1.00" && libraryVersion == "1.01") {
        s += "  LA 1.01 CORRIGE LE BORNAGE DE Count : en 1.00, `IF n > LAST THEN "
             "n := LAST` borne un NOMBRE par un INDICE, et la derniere voie ou le "
             "dernier equipement n'est jamais traite. Sur une carte de 16 voies, "
             "la voie 15 ne lit rien et rien ne le signale. La mise a jour n'est "
             "pas optionnelle.";
    } else {
        s += "  Ce que le projet fait est ce que SA version fait : les corrections "
             "apportees depuis ne s'y appliquent pas.";
    }
    s += "  (" + std::string(entryName) + ", voir XPG-4101)";
    return s;
}

} // namespace help
