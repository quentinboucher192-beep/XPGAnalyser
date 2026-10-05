#include "HelpDocument.hpp"

#include "../project/ImportSchema.hpp"

#include <algorithm>
#include <cctype>

namespace ui {

namespace {

// Les caracteres latins accentues, en UTF-8, vers leur lettre nue. La table est
// courte parce qu'elle n'a a couvrir que ce qu'on ecrit reellement : du
// francais, et les quelques lettres qu'un nom de materiel peut porter.
struct Fold { const char* utf8; char plain; };

constexpr Fold kFolds[] = {
    {"\xC3\xA0", 'a'}, {"\xC3\xA1", 'a'}, {"\xC3\xA2", 'a'}, {"\xC3\xA4", 'a'},
    {"\xC3\xA7", 'c'},
    {"\xC3\xA8", 'e'}, {"\xC3\xA9", 'e'}, {"\xC3\xAA", 'e'}, {"\xC3\xAB", 'e'},
    {"\xC3\xAC", 'i'}, {"\xC3\xAD", 'i'}, {"\xC3\xAE", 'i'}, {"\xC3\xAF", 'i'},
    {"\xC3\xB2", 'o'}, {"\xC3\xB3", 'o'}, {"\xC3\xB4", 'o'}, {"\xC3\xB6", 'o'},
    {"\xC3\xB9", 'u'}, {"\xC3\xBA", 'u'}, {"\xC3\xBB", 'u'}, {"\xC3\xBC", 'u'},
    {"\xC3\xBD", 'y'}, {"\xC3\xBF", 'y'}, {"\xC3\xB1", 'n'},
};

} // namespace

std::string HelpDocument::fold(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c)));
            ++i;
            continue;
        }
        bool done = false;
        if (i + 1 < s.size()) {
            for (const auto& f : kFolds) {
                if (s[i] == f.utf8[0] && s[i + 1] == f.utf8[1]) {
                    out.push_back(f.plain);
                    i += 2;
                    done = true;
                    break;
                }
            }
            // Les majuscules accentuees : meme octet de tete, second octet
            // decale de 0x20. Les traiter par la meme table evite de l'ecrire
            // deux fois - et d'en oublier la moitie.
            if (!done && s[i] == '\xC3') {
                const auto second = static_cast<unsigned char>(s[i + 1]);
                if (second >= 0x80 && second <= 0x9E) {
                    const char abaisse = static_cast<char>(second + 0x20);
                    for (const auto& f : kFolds) {
                        if (f.utf8[1] == abaisse) {
                            out.push_back(f.plain);
                            i += 2;
                            done = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!done) {
            // Un caractere qu'on ne sait pas replier est recopie tel quel : le
            // perdre ferait disparaitre un mot de la recherche.
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

std::string HelpDocument::slug(std::string_view title) {
    std::string out;
    bool tiret = false;
    for (char ch : fold(title)) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            out.push_back(ch);
            tiret = false;
        } else if (!out.empty() && !tiret) {
            out.push_back('-');
            tiret = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// ---------------------------------------------------------------------------
HelpDocument& HelpDocument::heading(int level, std::string text, std::string anchor) {
    Block b;
    b.kind = BlockKind::Heading;
    b.level = std::clamp(level, 1, 3);
    b.text = std::move(text);
    b.anchor = anchor.empty() ? slug(b.text) : std::move(anchor);

    // DEUX TITRES IDENTIQUES ONT DES ANCRES DIFFERENTES. "Colonnes" apparait
    // sous chacun des six formats ; sans suffixe, le sommaire renverrait les six
    // fois au premier.
    std::string base = b.anchor;
    int n = 2;
    while (blockOf(b.anchor) != blocks_.size())
        b.anchor = base + "-" + std::to_string(n++);

    currentAnchor_ = b.anchor;
    toc_.push_back({b.text, b.anchor, b.level, blocks_.size()});
    blocks_.push_back(std::move(b));
    return *this;
}

HelpDocument& HelpDocument::paragraph(std::string text) {
    blocks_.push_back({BlockKind::Paragraph, 0, std::move(text), {}, false, currentAnchor_});
    return *this;
}

HelpDocument& HelpDocument::bullet(std::string text) {
    blocks_.push_back({BlockKind::Bullet, 0, std::move(text), {}, false, currentAnchor_});
    return *this;
}

HelpDocument& HelpDocument::code(std::string text) {
    blocks_.push_back({BlockKind::Code, 0, std::move(text), {}, false, currentAnchor_});
    return *this;
}

HelpDocument& HelpDocument::tableRow(std::vector<std::string> cells, bool header) {
    Block b;
    b.kind = BlockKind::TableRow;
    b.cells = std::move(cells);
    b.header = header;
    b.anchor = currentAnchor_;
    blocks_.push_back(std::move(b));
    return *this;
}

HelpDocument& HelpDocument::separator() {
    blocks_.push_back({BlockKind::Separator, 0, {}, {}, false, currentAnchor_});
    return *this;
}

HelpDocument& HelpDocument::indexTerm(std::string term) {
    terms_.emplace_back(std::move(term), currentAnchor_);
    return *this;
}

// ---------------------------------------------------------------------------
std::size_t HelpDocument::blockOf(std::string_view anchor) const {
    if (anchor.empty()) return blocks_.size();
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        if (blocks_[i].kind == BlockKind::Heading && blocks_[i].anchor == anchor)
            return i;
    return blocks_.size();
}

std::string HelpDocument::sectionOf(std::size_t block) const {
    if (block >= blocks_.size()) return {};
    // On remonte jusqu'au titre precedent. Le bloc porte deja son ancre, mais
    // c'est le LIBELLE qu'on veut afficher.
    for (std::size_t i = block + 1; i-- > 0;) {
        if (blocks_[i].kind == BlockKind::Heading) return blocks_[i].text;
        if (i == 0) break;
    }
    return {};
}

std::vector<IndexEntry> HelpDocument::index() const {
    std::vector<IndexEntry> out;
    for (const auto& [terme, ancre] : terms_) {
        auto at = std::find_if(out.begin(), out.end(),
                               [&](const IndexEntry& e) { return e.term == terme; });
        if (at == out.end()) {
            out.push_back({terme, {ancre}});
        } else if (std::find(at->anchors.begin(), at->anchors.end(), ancre)
                   == at->anchors.end()) {
            at->anchors.push_back(ancre);
        }
    }

    // LE TRI IGNORE LES ACCENTS. Sans ca "Echelle" et "Echantillon" se
    // retrouvent separes par tout l'alphabet des que l'un porte un accent, et
    // l'index devient inutilisable precisement sur les mots francais.
    std::sort(out.begin(), out.end(), [](const IndexEntry& a, const IndexEntry& b) {
        const auto fa = fold(a.term), fb = fold(b.term);
        if (fa != fb) return fa < fb;
        return a.term < b.term;
    });
    return out;
}

std::vector<SearchHit> HelpDocument::search(std::string_view needle,
                                            std::size_t limit) const {
    std::vector<SearchHit> out;
    const auto cible = fold(needle);
    if (cible.empty()) return out;

    for (std::size_t i = 0; i < blocks_.size() && out.size() < limit; ++i) {
        const auto& b = blocks_[i];

        std::string texte = b.text;
        for (const auto& c : b.cells) {
            if (!texte.empty()) texte += "  ";
            texte += c;
        }
        if (texte.empty()) continue;

        const auto plie = fold(texte);
        const auto at = plie.find(cible);
        if (at == std::string::npos) continue;

        // L'EXTRAIT EST DECOUPE SUR LE TEXTE D'ORIGINE, pas sur la version
        // repliee : le lecteur doit voir sa phrase avec ses accents. Les deux
        // ont la meme longueur en octets parce que fold remplace deux octets par
        // un seul... ce qui est faux. On se cale donc sur le texte d'origine en
        // reperant le mot par recherche insensible, et a defaut on montre le
        // debut du bloc - toujours quelque chose de lisible.
        SearchHit h;
        h.block = i;
        h.anchor = b.anchor;
        h.section = sectionOf(i);

        constexpr std::size_t kAutour = 60;
        std::size_t debut = 0;
        if (plie.size() == texte.size() && at > kAutour) debut = at - kAutour;
        h.excerpt = texte.substr(debut, kAutour * 2 + cible.size());
        if (debut > 0) h.excerpt = "..." + h.excerpt;
        if (debut + h.excerpt.size() < texte.size()) h.excerpt += "...";

        out.push_back(std::move(h));
    }
    return out;
}

// ---------------------------------------------------------------------------
HelpDocument buildHelp() {
    using namespace project;
    HelpDocument d;

    d.heading(1, "Aide", "aide");
    d.paragraph("Cette page est construite depuis les m\xC3\xAAmes d\xC3\xA9" "clarations que les "
                "importateurs. Elle ne peut donc pas d\xC3\xA9" "crire un format que le "
                "programme ne lit plus, ni oublier une colonne qu'il attend.");

    // ---- 1.10 : les nouveautes de la 1.10, cote automate (chantier P / H) ----
    //  L'ancre nouveautes-1-10 : HelpView encadre la section en orange tant que
    //  le lecteur n'a pas vu la 1.10 (ui::novelty::sinceLabel). Le detail des
    //  ecrans de l'IHM est dans l'onglet IHM (sujet nouveautes-1-10 du guide).
    d.heading(2, "Nouveaut\xC3\xA9s de la 1.10", "nouveautes-1-10");
    d.indexTerm("nouveaut\xC3\xA9s de la 1.10");
    d.indexTerm("grafcet");
    d.indexTerm("F8");
    d.paragraph("La 1.10 refait l'\xC3\xA9" "diteur de grafcet, s\xC3\xA9pare la simulation de l'automate de celle de l'IHM, "
                "range le menu Aide et montre ses nouveaut\xC3\xA9s. Le d\xC3\xA9tail des \xC3\xA9" "crans de l'IHM est dans "
                "l'onglet IHM de l'aide (F1), sujet \xC2\xAB Les nouveaut\xC3\xA9s de la 1.10 \xC2\xBB.");
    d.bullet("L'\xC3\xA9" "diteur de grafcet refait : les grafcets des instances de DFB_GRAFCETENGINE, en fran\xC3\xA7" "ais, "
             "dessin\xC3\xA9s selon le GRAFCET (\xC3\xA9tapes, transitions et r\xC3\xA9" "ceptivit\xC3\xA9s, divergences en OU et en ET, "
             "actions avec leur genre) ; le navigateur des instances du programme ; l'\xC3\xA9" "dition r\xC3\xA9\xC3\xA9" "crit le "
             "programme par une commande annulable ; en simulation, les \xC3\xA9tapes actives, les dur\xC3\xA9" "es et les "
             "commandes du moteur ; l'onglet Contr\xC3\xB4les et l'Historique.");
    d.bullet("Le code du grafcet en direct : sous le dessin, le volet Code \xC3\xA9" "dite la r\xC3\xA9" "ceptivit\xC3\xA9 ou "
             "l'action choisie avec l'\xC3\xA9" "diteur des sections ST (aide \xC3\xA0 la saisie, X3, FIN(A2), DUREE(X1) >= T#5s) ; "
             "les onglets ST ouverts sur ces sections suivent aussit\xC3\xB4t, et une section modifi\xC3\xA9" "e en ST "
             "redessine le grafcet.");
    d.bullet("L'automate et l'IHM simul\xC3\xA9s chacun de son c\xC3\xB4t\xC3\xA9 : D\xC3\xA9marrer l'API (F5), D\xC3\xA9marrer "
             "l'IHM (F8, Maj+F8 l'arr\xC3\xAAte), ou les deux ; deux pastilles disent en permanence ce qui tourne.");
    d.bullet("Le menu Aide rang\xC3\xA9 en quatre blocs, une seule entr\xC3\xA9" "e Nouveaut\xC3\xA9s\xE2\x80\xA6 (la fen\xC3\xAAtre "
             "des nouveaut\xC3\xA9s, Me montrer), les rep\xC3\xA8res des nouveaut\xC3\xA9s en case \xC3\xA0 cocher, \xC3\x80 propos "
             "d'XPGAnalyser.");
    d.bullet("Les nouveaut\xC3\xA9s se voient : \xC3\xA0 la premi\xC3\xA8re ouverture d'une nouvelle version, la fen\xC3\xAAtre "
             "des nouveaut\xC3\xA9s ; les \xC3\xA9l\xC3\xA9ments nouveaux encadr\xC3\xA9s en orange (NOUVEAU) ; dans l'aide, ce qui "
             "est nouveau depuis la derni\xC3\xA8re version que tu as vue, encadr\xC3\xA9 en orange.");

    // ---- 1.9 : les nouveautes de la 1.9, cote automate (chantier H) ----
    //  Le detail des ecrans est dans l'onglet IHM (sujet nouveautes-1-9 du
    //  guide) ; ici, ce qui touche l'automate, son programme et ses types.
    d.heading(2, "Nouveaut\xC3\xA9s de la 1.9", "nouveautes-1-9");
    d.indexTerm("nouveaut\xC3\xA9s de la 1.9");
    d.indexTerm("esclave simul\xC3\xA9");
    d.indexTerm("lecture cyclique");
    d.indexTerm("page Simulation");
    d.paragraph("La 1.9 touche l'automate et son programme par la simulation des \xC3\xA9quipements, l'outil Modbus "
                "et les types. Le d\xC3\xA9tail de chaque \xC3\xA9" "cran est dans l'onglet IHM de l'aide (F1), sujet "
                "\xC2\xAB Les nouveaut\xC3\xA9s de la 1.9 \xC2\xBB.");
    d.bullet("Les esclaves simul\xC3\xA9s : un \xC3\xA9quipement Modbus TCP du r\xC3\xA9seau (un variateur, une "
             "centrale de mesure) peut avoir son esclave simul\xC3\xA9 li\xC3\xA9 - un esclave Modbus dans l'application, "
             "\xC3\xA0 la configuration du vrai, li\xC3\xA9 \xC3\xA0 lui (le cadenas). Le mot \xC2\xAB jumeau "
             "\xC2\xBB a quitt\xC3\xA9 l'\xC3\xA9" "cran. L'IHM lit le vrai appareil, l'esclave, ou bascule toute "
             "seule sur l'esclave quand le vrai ne r\xC3\xA9pond plus (Basculer apr\xC3\xA8s : 10 s) ; chaque "
             "bascule est un \xC3\xA9v\xC3\xA9nement Communication et une marque sur les courbes. L'automate du "
             "projet reste celui du simulateur (Simulation \xE2\x80\xBA Automate) ou l'automate r\xC3\xA9" "el "
             "(IHM \xE2\x80\xBA Configuration \xE2\x80\xBA Communication).");
    d.bullet("Les lectures simul\xC3\xA9" "es : en marche, ce qui est lu sur un esclave simul\xC3\xA9 se voit "
             "en violet - un cadre en tirets et une pastille sur l'objet, le bandeau LECTURES SIMUL\xC3\x89" "ES, "
             "la barre d'\xC3\xA9tat.");
    d.bullet("La page Simulation : le 3e onglet de Param\xC3\xA8tres syst\xC3\xA8me, r\xC3\xA9serv\xC3\xA9 \xC3\xA0 "
             "la permission Administrer (Ctrl+Alt+S sur le poste d'exploitation), commande tous les esclaves simul\xC3\xA9s "
             ": ce que lit l'IHM, la marche, la panne, une exception, les valeurs anim\xC3\xA9" "es et forc\xC3\xA9" "es. "
             "Ses choix durent jusqu'au red\xC3\xA9marrage de l'IHM.");
    d.bullet("Les variables syst\xC3\xA8me des esclaves : SYS.SimSlaveCount, SYS.SimReads, SYS.SimFallbacks\xE2\x80\xA6 "
             "et une structure SYS.Slave.<nom> par esclave (Running, Read, Fallback, Mode, Port, Requests\xE2\x80\xA6) "
             "; IHM_ESCLAVE_SIMULE('\xC3\x89quipement') dans une expression.");
    d.bullet("L'outil Modbus : la lecture cyclique lit plusieurs requ\xC3\xAAtes \xC3\xA0 la fois, chacune vers "
             "sa cible - un \xC3\xA9quipement, l'automate du projet (%MW, %M, %MF, %MD), une adresse, ou l'esclave "
             "simul\xC3\xA9 d'un \xC3\xA9quipement - et \xC3\xA0 sa p\xC3\xA9riode, une connexion par \xC3\xA9quipement. "
             "\xC2\xAB Depuis des variables\xE2\x80\xA6 \xC2\xBB fait les requ\xC3\xAAtes d'apr\xC3\xA8s les variables "
             "localis\xC3\xA9" "es du programme (les voisines regroup\xC3\xA9" "es) ; les jeux de lecture s'enregistrent "
             "dans le projet ; l'export CSV et l'enregistrement continu vont dans exports/modbus/.");
    d.bullet("Les DDT comme types : une popup de l'IHM d\xC3\xA9" "clare ses param\xC3\xA8tres avec un type, et "
             "les DDT du programme (API \xE2\x80\xBA Types d\xC3\xA9riv\xC3\xA9s) en font partie, \xC3\xA0 c\xC3\xB4t\xC3\xA9 "
             "des types de base et des types IHM (voir \xC2\xAB Les DDT dans les popups de l'IHM \xC2\xBB).");
    d.bullet("Les alarmes des symboles et des objets du synoptique (onglet IHM) s'ajoutent aux alarmes de l'automate "
             "(DFB_ALM_MANAGER, voir \xC2\xAB Les alarmes \xC2\xBB) sans les remplacer.");

    d.heading(2, "Les DDT dans les popups de l'IHM", "ddt-popups");
    d.indexTerm("DDT");
    d.indexTerm("param\xC3\xA8tre de popup");
    d.indexTerm("type d\xC3\xA9riv\xC3\xA9");
    d.paragraph("Un type d\xC3\xA9riv\xC3\xA9 du programme (un DDT : une structure, ou un tableau) sert de type \xC3\xA0 "
                "un param\xC3\xA8tre de popup de l'IHM : dans la liste des param\xC3\xA8tres de la popup, le choix "
                "du type a une colonne DDT de l'API (chaque DDT avec son nombre de membres, import\xC3\xA9s du .XPG).");
    d.bullet("L'aide \xC3\xA0 la saisie propose ses membres apr\xC3\xA8s un point : Moteur. propose Marche, Vitesse\xE2\x80\xA6 "
             "comme dans le programme.");
    d.bullet("En mode Copie ou Les deux, l'ouverture de la popup capture tous ses membres ; Appliquer copie sur "
             "r\xC3\xA9" "f\xC3\xA9rence ne r\xC3\xA9\xC3\xA9" "crit dans l'automate que les membres modifi\xC3\xA9s "
             "depuis l'ouverture.");
    d.bullet("Un argument doit \xC3\xAAtre du m\xC3\xAAme DDT ; un DDT renomm\xC3\xA9 ou retir\xC3\xA9 du programme "
             "devient un type inconnu, que G\xC3\xA9n\xC3\xA9rer signale sur chaque param\xC3\xA8tre qui le cite.");
    d.bullet("Sans programme charg\xC3\xA9, la colonne le dit, et un argument de l'automate n'est pas v\xC3\xA9rifi\xC3\xA9.");
    // ---- fin 1.9 ----

    // ---- Lot API 8 : didacticiels et aide ----
    //  LES NOUVEAUTES DU LOT 8, chacune avec son didacticiel (Aide > Nouveautes
    //  du lot 8 ouvre leurs cartes), LES RACCOURCIS CLAVIER, puis une page par
    //  nouvel ecran : F1 sur un onglet du dossier Simulation ouvre la sienne.
    //  Les ancres sont celles de app/TutorialsLot8 (le test lot8tutorials les
    //  verifie toutes).
    // 1.11, decision 12 : plus de nom de chantier dans un titre visible du centre (l'ancre reste).
    d.heading(2, "Nouveaut\xC3\xA9s : la simulation, le d\xC3\xA9" "bogage, les fichiers", "nouveautes-lot8");
    d.indexTerm("nouveaut\xC3\xA9s de la 1.8.0");
    d.indexTerm("didacticiel");
    d.paragraph("Chaque nouveaut\xC3\xA9 a son didacticiel : Aide \xE2\x80\xBA Nouveaut\xC3\xA9s\xE2\x80\xA6 \xE2\x80\xBA Versions pr\xC3\xA9" "c\xC3\xA9" "dentes ouvre leurs cartes (Commencer, Reprendre, "
                "Refaire) ; ils sont aussi dans Aide \xE2\x80\xBA Didacticiel de l'API, marqu\xC3\xA9s NOUVEAU.");
    d.bullet("Le dossier Simulation, au m\xC3\xAAme niveau qu'API, IHM et Versions : Vue d'ensemble, Automate (l'ancien API \xE2\x80\xBA "
             "Simulation), IHM (l'ancien IHM \xE2\x80\xBA Simulation), \xC3\x89quipements, D\xC3\xA9" "bogage, For\xC3\xA7" "ages, Courbes, Journal. F9 l'ouvre, F5 "
             "Simuler / Continuer, Maj+F5 Arr\xC3\xAAter, F10 Section suivante. Didacticiel \xC2\xAB Simuler et suivre la simulation \xC2\xBB.");
    d.bullet("Le d\xC3\xA9" "bogage : les points d'arr\xC3\xAAt dans la marge du code, leur condition, les espions, qui a \xC3\xA9" "crit, le pas \xC3\xA0 pas. "
             "Didacticiel \xC2\xAB D\xC3\xA9" "boguer pas \xC3\xA0 pas \xC2\xBB.");
    d.bullet("Modifier le projet pendant une pause : la simulation reprend au m\xC3\xAAme cycle. Didacticiel \xC2\xAB Modifier pendant une "
             "pause \xC2\xBB.");
    d.bullet("Glisser n'importe quel fichier : d'un classeur, tout ce qu'on peut en faire, \xC3\xA0 cocher ; d'un autre fichier, "
             "Ressources et / ou Fichiers externes. Didacticiel \xC2\xAB Glisser un fichier dans le projet \xC2\xBB.");
    d.bullet("Les th\xC3\xA8mes : 43 th\xC3\xA8mes par famille ; cr\xC3\xA9" "er, modifier, exporter, importer un .xpgtheme. Didacticiel \xC2\xAB Cr\xC3\xA9" "er ou "
             "modifier un th\xC3\xA8me \xC2\xBB.");
    d.bullet("Renommer partout : la case Nom, F2 et le double-clic ouvrent le dialogue Renommer ; les expressions des objets "
             "suivent ; un champ de DDT aussi. Didacticiel \xC2\xAB Renommer partout \xC2\xBB.");
    d.bullet("Les expressions impossibles signal\xC3\xA9" "es par Compiler et G\xC3\xA9n\xC3\xA9rer, et le symbole fx. Didacticiel \xC2\xAB Expressions : "
             "Compiler et G\xC3\xA9n\xC3\xA9rer \xC2\xBB.");
    d.bullet("Les champs de recherche et les filtres retenus d'une s\xC3\xA9" "ance \xC3\xA0 l'autre. Didacticiel \xC2\xAB Chercher et retrouver ses "
             "filtres \xC2\xBB.");
    d.bullet("Les exports demandent o\xC3\xB9 enregistrer ; les dialogues s'ouvrent dans la fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e, l\xC3\xA0 o\xC3\xB9 tu regardes.");
    // ---- Lot API 8 : l'explorateur, l'arbre, les bandeaux (l'aide) ----
    d.bullet("L'explorateur de fichiers de l'appli (le bouton \xE2\x80\xA6 d'un import ou d'un export) : aux couleurs de l'appli, "
             "les dossiers du projet \xC3\xA0 gauche, un aper\xC3\xA7u \xC3\xA0 droite (ce que contient un .XPG, un classeur, une image), "
             "les vignettes.");
    d.bullet("L'arbre du projet : un filtre en haut (Ctrl+Maj+F), les outils sous le titre de chaque domaine, des pastilles "
             "(erreurs, mises \xC3\xA0 jour, modifi\xC3\xA9 depuis la derni\xC3\xA8re version), \xC3\x89pingl\xC3\xA9s et R\xC3\xA9" "cents, le rail \xC3\xA0 gauche, "
             "la sant\xC3\xA9 du projet en pied.");
    d.bullet("Les bandeaux : en haut, le projet et Enregistrer, la version, la palette Aller \xC3\xA0 / Faire\xE2\x80\xA6, la simulation "
             "en couleur, la cloche des notifications, les t\xC3\xA2" "ches de fond ; en bas, le journal des messages, ce qui est choisi, "
             "les erreurs, le zoom, le th\xC3\xA8me, l'enregistrement.");
    // ---- fin Lot API 8 : l'explorateur, l'arbre, les bandeaux ----

    d.heading(2, "Raccourcis clavier", "raccourcis");
    d.indexTerm("raccourcis");
    d.indexTerm("clavier");
    d.indexTerm("F5");
    d.indexTerm("F9");
    d.indexTerm("F8");   // 1.10 (chantier L) : l'IHM simulee
    d.indexTerm("F2");
    d.paragraph("Depuis la 1.8.0, F5 ne relance plus l'analyse : Projet \xE2\x80\xBA R\xC3\xA9importer et r\xC3\xA9" "analyser le fait.");
    d.tableRow({"Touche", "Ce qu'elle fait"}, true);
    d.tableRow({"F1", "L'aide de ce qui est sous le curseur ou de l'onglet ouvert (un onglet du dossier Simulation : sa page)"});
    d.tableRow({"F2", "Renommer ce qui est choisi : le dialogue Renommer (aussi la case Nom et le double-clic)"});
    d.tableRow({"F5", "Simuler ; en pause, Continuer"});
    d.tableRow({"Maj+F5", "Arr\xC3\xAAter la simulation"});
    // 1.10 (chantier L) : l'IHM simulee a ses touches, a cote de celles de l'API.
    d.tableRow({"F8", "D\xC3\xA9marrer l'IHM simul\xC3\xA9" "e (l'API ne d\xC3\xA9marre pas avec elle)"});
    d.tableRow({"Maj+F8", "Arr\xC3\xAAter l'IHM simul\xC3\xA9" "e (l'API continue)"});
    d.tableRow({"F9", "Simulation \xE2\x80\xBA Vue d'ensemble ; dans le code d'une section : un point d'arr\xC3\xAAt sur la ligne du curseur"});
    d.tableRow({"Ctrl+F9", "Dans le code d'une section : activer ou d\xC3\xA9sactiver le point d'arr\xC3\xAAt de la ligne"});
    d.tableRow({"F10", "Section suivante (en pause) ; Ctrl+F10 : jusqu'\xC3\xA0 la ligne du curseur (Simulation \xE2\x80\xBA D\xC3\xA9" "bogage)"});
    d.tableRow({"F11", "Plein \xC3\xA9" "cran, et retour ; dans Simulation \xE2\x80\xBA IHM : le simulateur IHM en plein \xC3\xA9" "cran, la vue seule "
                       "(\xC3\x89" "chap ou F11 pour sortir) ; dans Simulation \xE2\x80\xBA D\xC3\xA9" "bogage : Cycle suivant (le programme va jusqu'\xC3\xA0 la fin du cycle)"});
    d.tableRow({"Ctrl+K", "Aller \xC3\xA0\xE2\x80\xA6 : cherche partout (variables et leurs membres, vues, objets, le code ligne \xC3\xA0 ligne)"});
    d.tableRow({"Ctrl+F", "Le champ de recherche de l'onglet ouvert (aussi dans une fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e), son texte choisi ; "
                          "\xC3\x89" "chap l'efface (ses filtres sont retenus d'une s\xC3\xA9" "ance \xC3\xA0 l'autre)"});
    // ---- Lot API 8 : l'arbre du projet ----
    d.tableRow({"Ctrl+Maj+F", "Filtrer l'arbre du projet : le curseur dans le champ au-dessus de l'arbre (il cherche aussi dans le "
                              "contenu : sections, types, vues, variables, recettes, alarmes, scripts) ; \xC3\x89" "chap l'efface"});
    // ---- fin Lot API 8 ----
    d.tableRow({"Ctrl+Z / Ctrl+Y", "Annuler / R\xC3\xA9tablir, partout"});
    d.tableRow({"Ctrl+H", "L'Historique"});
    d.tableRow({"Ctrl+S / Ctrl+O", "Enregistrer / Ouvrir"});
    d.tableRow({"Ctrl+Maj+O", "Importer la configuration mat\xC3\xA9rielle (.XHW) dans le projet ouvert (1.11 : Ctrl+H reste \xC3\xA0 l'Historique)"});
    d.tableRow({"Ctrl+Alt+S", "Une version du projet (sur le poste d'exploitation : la page Simulation de Param\xC3\xA8tres syst\xC3\xA8me, 1.9)"});
    d.tableRow({"Ctrl+W", "Fermer l'onglet ouvert"});
    d.tableRow({"Ctrl+1 / Ctrl+5", "API \xE2\x80\xBA Variables / API \xE2\x80\xBA Statistiques"});
    d.tableRow({"Suppr", "Supprimer ce qui est choisi"});
    d.tableRow({"Ctrl+Maj+H", "Revenir au menu principal"});

    d.heading(2, "Simulation \xE2\x80\xBA Vue d'ensemble", "sim-ensemble");
    d.indexTerm("simulation");
    d.indexTerm("vue d'ensemble");
    d.paragraph("Le dossier Simulation est au m\xC3\xAAme niveau que API, IHM et Versions : tout ce qui concerne la simulation est "
                "l\xC3\xA0 (avant, c'\xC3\xA9tait \xC3\xA9parpill\xC3\xA9 entre API \xE2\x80\xBA Simulation et IHM \xE2\x80\xBA Simulation, devenus Simulation \xE2\x80\xBA Automate et "
                "Simulation \xE2\x80\xBA IHM). La pastille du dossier dit en un mot o\xC3\xB9 on en est : en marche, pause, arr\xC3\xAAt, d\xC3\xA9" "faut.");
    d.bullet("Le bandeau r\xC3\xA9pond \xC3\xA0 \xC2\xAB est-ce que \xC3\xA7" "a tourne ? \xC2\xBB en une phrase, avec une couleur : vert tout va bien, bleu en "
             "pause, orange point d'arr\xC3\xAAt, rouge l'automate est arr\xC3\xAAt\xC3\xA9.");
    d.bullet("Les commandes sont toujours dans la barre du haut, quel que soit l'onglet : Simuler (F5), Pause, Un cycle, "
             "Arr\xC3\xAAter (Maj+F5), et l'\xC3\xA9tat ; un clic sur l'\xC3\xA9tat ram\xC3\xA8ne ici (F9 aussi). "
             "\xC3\x80 c\xC3\xB4t\xC3\xA9, l'IHM simul\xC3\xA9" "e a les siennes : son \xC3\xA9tat, D\xC3\xA9marrer (F8), Arr\xC3\xAAter (Maj+F8), et le menu "
             "Les deux (D\xC3\xA9marrer les deux, Tout arr\xC3\xAAter) : l'une ne d\xC3\xA9marre ni n'arr\xC3\xAAte l'autre sans le dire.");
    d.bullet("Ce qui m\xC3\xA9rite ton attention : les probl\xC3\xA8mes, du plus grave au moins grave, chacun avec le bouton qui le r\xC3\xA8gle. "
             "La cha\xC3\xAEne Automate \xE2\x86\x92 IHM \xE2\x86\x92 \xC3\xA9quipements montre les \xC3\xA9" "changes en direct ; un maillon cass\xC3\xA9 devient rouge.");
    d.paragraph("Didacticiel : \xC2\xAB Simuler et suivre la simulation \xC2\xBB (Aide \xE2\x80\xBA Didacticiel de l'API).");

    d.heading(2, "Simulation \xE2\x80\xBA D\xC3\xA9" "bogage", "sim-debogage");
    d.indexTerm("d\xC3\xA9" "bogage");
    d.indexTerm("point d'arr\xC3\xAAt");
    d.indexTerm("espion");
    d.bullet("Un point d'arr\xC3\xAAt : un clic dans la marge du code d'une section, \xC3\xA0 gauche du num\xC3\xA9ro de ligne (F9 sur la ligne du "
             "curseur, Ctrl+F9 l'active ou le d\xC3\xA9sactive). Sans condition, la simulation s'arr\xC3\xAAte au prochain passage.");
    d.bullet("Sa condition : un double-clic sur la colonne Condition de la liste des points d'arr\xC3\xAAt (armoires[0].etat = 6) ; la "
             "simulation ne s'arr\xC3\xAAte que quand elle est vraie.");
    d.bullet("En pause : le bandeau dit o\xC3\xB9 (la section, la ligne) et quand (le cycle) ; la ligne est surlign\xC3\xA9" "e. Section "
             "suivante (F10) ex\xC3\xA9" "cute la section et s'arr\xC3\xAAte au d\xC3\xA9" "but de la suivante, dans l'ordre de MAST ; la trace du cycle "
             "montre o\xC3\xB9 tu en es.");
    // 1.11.2 (T2, decision 143) : une section dont la condition d'activation est fausse ne tourne pas, comme sur
    // l'automate (sim::Runtime ; la trace : SimDebugPane.cpp, "inactive (<condition> faux)") ; le cas d'Armoire_Gaz.
    d.bullet("Une section ne tourne que si sa condition d'activation est vraie, comme sur l'automate. Sinon, la trace du cycle "
             "le dit (\xC2\xAB inactive (configuree faux) \xC2\xBB) : son code ne s'ex\xC3\xA9" "cute pas, et un point d'arr\xC3\xAAt pos\xC3\xA9 "
             "dedans n'arr\xC3\xAAte rien. Dans Armoire_Gaz, presque toutes les sections des unit\xC3\xA9s ont pour condition configuree, "
             "que leur section Init recopie de ConfigArmoireUtilisee.configuree, vraie \xC3\xA0 la validation d'une configuration "
             "d'armoire (page 151) ; pour les faire tourner avant, force ConfigArmoireUtilisee.configuree (Simulation \xE2\x80\xBA "
             "Automate, double-clic sur sa valeur).");
    d.bullet("Pourquoi ici (la condition, sa valeur, le num\xC3\xA9ro de passage) et qui a \xC3\xA9" "crit l'espion choisi : la section, la "
             "ligne, le cycle, avec un bouton pour y aller. Continuer (F5) repart.");
    d.paragraph("Didacticiel : \xC2\xAB D\xC3\xA9" "boguer pas \xC3\xA0 pas \xC2\xBB.");

    d.heading(2, "Modifier pendant une pause", "sim-pause");
    d.indexTerm("pause");
    d.paragraph("En pause, tu peux modifier le projet. Au prochain Continuer (F5), la simulation reprend au m\xC3\xAAme cycle avec ta "
                "modification ; avant, elle repartait du cycle 0. Le journal le note (Simulation \xE2\x80\xBA Journal). Didacticiel : "
                "\xC2\xAB Modifier pendant une pause \xC2\xBB.");

    d.heading(2, "Simulation \xE2\x80\xBA For\xC3\xA7" "ages", "sim-forcages");
    d.indexTerm("for\xC3\xA7" "age");
    d.paragraph("Tout ce qui est forc\xC3\xA9, de l'automate et des \xC3\xA9quipements, au m\xC3\xAAme endroit ; un clic rel\xC3\xA2" "che une ligne, Tout "
                "rel\xC3\xA2" "cher les rel\xC3\xA2" "che toutes.");
    d.paragraph("1.9 : les cases forc\xC3\xA9" "es des esclaves simul\xC3\xA9s y sont aussi ; en marche, un administrateur les r\xC3\xA8gle sur la "
                "page Simulation de Param\xC3\xA8tres syst\xC3\xA8me (Ctrl+Alt+S sur le poste d'exploitation).");

    d.heading(2, "Simulation \xE2\x80\xBA Courbes", "sim-courbes");
    d.indexTerm("courbes");
    d.paragraph("Les variables suivies, de l'automate ou de l'IHM, sur une m\xC3\xAAme \xC3\xA9" "chelle de temps (le temps simul\xC3\xA9).");

    d.heading(2, "Simulation \xE2\x80\xBA Journal", "sim-journal");
    d.indexTerm("journal");
    d.paragraph("Ce qui s'est pass\xC3\xA9, cycle par cycle : Simuler, les pauses, les points d'arr\xC3\xAAt, les modifications \xC3\xA0 chaud, "
                "les d\xC3\xA9" "fauts ; chaque ligne m\xC3\xA8ne \xC3\xA0 son endroit.");

    d.heading(2, "Glisser un fichier dans le projet", "glisser");
    d.indexTerm("glisser");
    d.indexTerm("d\xC3\xA9poser");
    d.indexTerm("classeur");
    d.bullet("Glisse un fichier n'importe o\xC3\xB9 sur l'appli : l'appli te dit tout ce qu'elle peut en faire.");
    d.bullet("Un classeur : une ligne par possibilit\xC3\xA9, avec son d\xC3\xA9tail (combien de variables, quelles colonnes sont "
             "reconnues, ce qui sera mis \xC3\xA0 jour) ; les lignes s\xC3\xBBres sont d\xC3\xA9j\xC3\xA0 coch\xC3\xA9" "es, les impossibles gris\xC3\xA9" "es avec leur "
             "raison ; une phrase dit ce qui va se passer, dans l'ordre ; Faire. Chaque import se d\xC3\xA9" "fait avec Ctrl+Z.");
    d.bullet("Un autre fichier (un PDF, une image, un son) : deux cases par fichier - Ressources (une copie dans le projet, "
             "qui voyage avec lui) et Fichiers externes (un lien vers le fichier d'origine, sans copie).");
    d.bullet("Un .XPG ou un .XHW : l'importer (le r\xC3\xA9" "capitulatif d'abord) ou l'ouvrir comme un projet s\xC3\xA9par\xC3\xA9.");
    // 1.11.2 (T2, tranche 44 ; decision 188) : un paquet glisse sur l'appli ouvre la fenetre d'import (REP, paquets-1112,
    // 4cb058c : filesDropped, HmiImportDialog::takePackages ; plusieurs fichiers : l'un apres l'autre).
    d.bullet("Un fichier fait par Exporter (des vues .xpgvues, des symboles .xpgsymboles, des types .xpgtypes, des fonctions "
             ".xpgfonctions, des scripts .xpgscripts) : la fen\xC3\xAAtre d'import s'ouvre, comme par Importer\xE2\x80\xA6 ; Annuler la "
             "referme sans rien changer.");
    d.paragraph("Didacticiel : \xC2\xAB Glisser un fichier dans le projet \xC2\xBB.");

    d.heading(2, "Les th\xC3\xA8mes", "themes");
    d.indexTerm("th\xC3\xA8me");
    d.indexTerm(".xpgtheme");
    d.bullet("Affichage \xE2\x80\xBA Th\xC3\xA8me\xE2\x80\xA6 : 43 th\xC3\xA8mes par famille (Sombres, Clairs, Color\xC3\xA9s, Contraste \xC3\xA9lev\xC3\xA9, "
             "Industriels) ; chaque carte est l'appli en miniature ; un clic applique, et le th\xC3\xA8me est retenu.");
    d.bullet("Nouveau : un nouveau th\xC3\xA8me \xC3\xA0 partir de celui qui est choisi, un nom (Cr\xC3\xA9" "er), l'\xC3\xA9" "diteur ; une pastille par "
             "couleur, toute l'appli change pendant que tu choisis. Les contrastes sont v\xC3\xA9rifi\xC3\xA9s ; Corriger les contrastes ne "
             "touche que la luminosit\xC3\xA9.");
    d.bullet("Enregistrer le range dans \xC2\xAB \xC3\x80 toi \xC2\xBB ; Modifier, Dupliquer, Renommer, Supprimer, Exporter\xE2\x80\xA6 (un fichier "
             ".xpgtheme, du texte lisible). Importer\xE2\x80\xA6 lit un .xpgtheme (le fichier, ou l\xC3\xA2" "ch\xC3\xA9 sur la galerie) ; un "
             "fichier ab\xC3\xAEm\xC3\xA9 est refus\xC3\xA9, avec la ligne et pourquoi.");
    d.paragraph("Didacticiel : \xC2\xAB Cr\xC3\xA9" "er ou modifier un th\xC3\xA8me \xC2\xBB.");

    d.heading(2, "Renommer partout", "renommer");
    d.indexTerm("renommer");
    d.bullet("F2, le clic droit, le bouton Renommer, la case Nom (elle ne renomme plus sur place) et le double-clic ouvrent le "
             "dialogue Renommer.");
    d.bullet("Le verdict du nom \xC3\xA0 chaque lettre ; o\xC3\xB9 \xC3\xA7" "a change, en onglets (Tout, API, IHM, Tables) et en arbre jusqu'\xC3\xA0 la "
             "propri\xC3\xA9t\xC3\xA9, avant (ancien nom barr\xC3\xA9) et apr\xC3\xA8s ; les expressions des objets suivent, et dans un texte \xC3\xA0 trous "
             "seul le trou change.");
    d.bullet("Un champ de DDT (API \xE2\x80\xBA Types d\xC3\xA9riv\xC3\xA9s, F2) : le code, les tables, l'IHM suivent ; un autre type n'est pas touch\xC3\xA9.");
    // Lot API 8 : finitions - les chaines qui designent une vue, une alarme, un objet.
    d.bullet("Les cha\xC3\xAEnes qui d\xC3\xA9signent une vue, une alarme ou un objet suivent : IHM_NAVIGUER('Vue_A'), SYS.CurrentView = 'Vue_A', "
             "SYS.AlarmSelected = 'Alarme_1', IHM_METTRE_DE_COTE('Alarme_1'), SYS.FocusedObject = 'Vue_A.Bouton', IHM_GIF_JOUER('Ventilateur') "
             "(un GIF, dans les scripts de sa vue) ; ailleurs (IHM_JOURNAL('Alarme_1 active')), une cha\xC3\xAEne reste un texte. Les scripts "
             "C / C++ : les noms, et les cha\xC3\xAEnes \"Vue_A.Bouton\".");
    d.bullet("Confirmer (ou Entr\xC3\xA9" "e) fait tout en une commande : un seul Ctrl+Z pour les deux c\xC3\xB4t\xC3\xA9s ; Annuler ne touche \xC3\xA0 rien.");
    d.paragraph("Didacticiel : \xC2\xAB Renommer partout \xC2\xBB.");

    d.heading(2, "Expressions : Compiler et G\xC3\xA9n\xC3\xA9rer", "compiler");
    d.indexTerm("expression");
    d.indexTerm("fx");
    d.indexTerm("Compiler");
    d.bullet("Le symbole fx : une propri\xC3\xA9t\xC3\xA9 li\xC3\xA9" "e \xC3\xA0 une expression (la pastille, la case teint\xC3\xA9" "e) ; au survol, l'expression "
             "et sa valeur. Le badge fx de la liste des objets dit combien ; rouge : une expression impossible.");
    d.bullet("IHM \xE2\x80\xBA Compiler : la liste des erreurs - o\xC3\xB9 (vue \xE2\x80\xBA objet \xE2\x80\xBA propri\xC3\xA9t\xC3\xA9), quoi, le bon nom propos\xC3\xA9 ; Aller \xC3\xA0, "
             "Remplacer, Tout remplacer.");
    d.bullet("IHM \xE2\x80\xBA G\xC3\xA9n\xC3\xA9rer refuse tant qu'il reste des erreurs, et dit lesquelles : rien de cass\xC3\xA9 ne part sur le pupitre.");
    // Lot API 8 : finitions - hors des vues.
    d.bullet("Compiler relit aussi Configuration \xE2\x80\xBA Unit\xC3\xA9s et formats (le format, le chemin \xE2\x80\x94 Armoires[].etat \xE2\x80\x94 \xC3\xA0 sa "
             "ligne), les symboles (leur d\xC3\xA9" "finition avec les valeurs par d\xC3\xA9" "faut de leurs param\xC3\xA8tres ; chaque instance avec "
             "ses arguments, l'erreur sur l'instance) et les mod\xC3\xA8les de vues gard\xC3\xA9s dans le projet (mod\xC3\xA8le, vue.objet "
             "(propri\xC3\xA9t\xC3\xA9)).");
    d.paragraph("Didacticiel : \xC2\xAB Expressions : Compiler et G\xC3\xA9n\xC3\xA9rer \xC2\xBB.");

    d.heading(2, "Chercher et retrouver ses filtres", "filtres");
    d.indexTerm("filtre");
    d.indexTerm("recherche");
    d.bullet("Un champ de recherche dans chaque volet (Ctrl+F y met le curseur) ; \xC2\xAB 3 sur 18 \xC2\xBB ; les mots trouv\xC3\xA9s surlign\xC3\xA9s ; "
             "plusieurs mots : tous, \"pompe 2\" la phrase exacte, -secours exclu.");
    d.bullet("Les recherches et les filtres par colonne sont retenus d'une s\xC3\xA9" "ance \xC3\xA0 l'autre : une pastille le dit (filtre de "
             "la derni\xC3\xA8re fois) ; Effacer quand tu n'en veux plus.");
    d.paragraph("Didacticiel : \xC2\xAB Chercher et retrouver ses filtres \xC2\xBB.");

    d.heading(2, "Les exports demandent o\xC3\xB9 enregistrer", "exports-lot8");
    d.indexTerm("exporter");
    d.paragraph("Un export ne part plus sans rien dire dans exports/ : le dossier du projet est propos\xC3\xA9, le bouton \xE2\x80\xA6 en "
                "choisit un autre, le chemin complet est montr\xC3\xA9. Les dialogues s'ouvrent dans la fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e quand tu y "
                "travailles.");

    // ---- 1.8.0 : les dossiers de l'application (app/Dossiers.hpp) ----
    d.heading(2, "Les dossiers de l'application", "dossiers");
    d.indexTerm("dossiers");
    d.indexTerm("XPGAnalyser.ini");
    d.indexTerm("dossier des donn\xC3\xA9" "es");
    d.paragraph("Install\xC3\xA9" "e, l'application range tes donn\xC3\xA9" "es hors du dossier du programme : Documents\\XPGAnalyser par d\xC3\xA9" "faut, "
                "ou le dossier choisi \xC3\xA0 l'installation. L'accueil (lien Dossiers) et Projet \xE2\x80\xBA Dossiers de l'application\xE2\x80\xA6 "
                "les montrent.");
    d.bullet("Donn\xC3\xA9" "es, Projets, Biblioth\xC3\xA8que (DFB, DDT, macros), Captures (F12) : leur chemin, d'o\xC3\xB9 il vient, Ouvrir "
             "(l'Explorateur de Windows) et Dossier\xE2\x80\xA6 pour en choisir un autre. Enregistrer l'\xC3\xA9" "crit dans "
             "%APPDATA%\\XpgAnalyzer\\XPGAnalyser.ini ; la case \xC2\xAB Copier le contenu \xC2\xBB y copie l'ancien (sans rien \xC3\xA9" "craser ; "
             "l'ancien dossier n'est jamais effac\xC3\xA9).");
    d.bullet("Projets et captures changent tout de suite ; les donn\xC3\xA9" "es et la biblioth\xC3\xA8que, au prochain d\xC3\xA9marrage.");
    d.bullet("R\xC3\xA9glages, Journaux, Programme : Ouvrir seulement. Ouvrir XPGAnalyser.ini : le fichier dans le Bloc-notes, avec "
             "ses explications (un chemin peut commencer par {Documents}, {Donnees}...).");
    d.bullet("La version de d\xC3\xA9veloppement (sans installation.ini \xC3\xA0 c\xC3\xB4t\xC3\xA9 de l'exe) garde ses dossiers dans son dossier "
             "de travail, comme avant : la fen\xC3\xAAtre les montre et les ouvre, sans les changer.");
    d.bullet("R\xC3\xA9parer, v\xC3\xA9rifier, restaurer une sauvegarde, collecter les journaux : menu D\xC3\xA9marrer \xE2\x80\xBA XPGAnalyser \xE2\x80\xBA "
             "Maintenance.");

    // ---- Lot API 8 : l'explorateur, l'arbre, les bandeaux (l'aide) ----
    d.heading(2, "L'explorateur de fichiers de l'appli", "explorateur-lot8");
    d.indexTerm("explorateur");
    d.indexTerm("parcourir");
    d.paragraph("Le bouton \xE2\x80\xA6 d'un champ de chemin (importer, exporter, choisir un dossier) ouvre l'explorateur de l'appli, "
                "\xC3\xA0 ses couleurs, dans la fen\xC3\xAAtre qui l'a demand\xC3\xA9.");
    d.bullet("En haut : Pr\xC3\xA9" "c\xC3\xA9" "dent, Suivant, Dossier parent, le chemin (chaque morceau se clique ; Ctrl+L le change en "
             "champ o\xC3\xB9 taper ou coller un chemin), Chercher (et ses sous-dossiers), D\xC3\xA9tails ou Vignettes, Nouveau dossier.");
    d.bullet("\xC3\x80 gauche, les emplacements : CE PROJET (son dossier, exports, simulation, ressources, le dossier du .XPG "
             "d'origine), R\xC3\xA9" "CENTS (les derniers dossiers choisis pour ce genre de demande), \xC3\x89PINGL\xC3\x89S (clic droit sur un "
             "dossier : \xC3\x89pingler), CE PC (Bureau, Documents, T\xC3\xA9l\xC3\xA9" "chargements, les lecteurs).");
    d.bullet("Au centre, la liste : les dossiers d'abord, un type parlant (Export Control Expert, Classeur Excel avec macros, "
             "Image PNG 1600 \xC3\x97 900), une pastille de couleur par famille de fichiers ; un filtre par genre (les fichiers "
             "cach\xC3\xA9s par le filtre sont compt\xC3\xA9s : \xC2\xAB Tout afficher \xC2\xBB). Clic droit : \xC3\x89pingler, Ouvrir avec le "
             "programme du syst\xC3\xA8me, Copier le chemin.");
    d.bullet("\xC3\x80 droite, l'aper\xC3\xA7u de l'\xC3\xA9l\xC3\xA9ment choisi : d'un .XPG le projet, l'automate, la date de l'export, ses "
             "sections, ses DFB et ses variables (et s'il est celui du projet ouvert, ou plus r\xC3\xA9" "cent) ; d'un .XHW l'automate, "
             "ses racks et ses modules ; d'un classeur ses onglets et le d\xC3\xA9" "but du premier ; d'un texte ses premi\xC3\xA8res "
             "lignes ; d'une image sa vignette ; d'un dossier ce qu'il contient.");
    d.bullet("En bas, le nom du fichier (enregistrer sur un fichier qui existe demande avant de le remplacer), le genre, "
             "Annuler et le bouton de la demande. Le dialogue du syst\xC3\xA8me reste possible : le lien \xC2\xAB Utiliser "
             "l'explorateur du syst\xC3\xA8me \xC2\xBB, en bas ; Affichage \xE2\x80\xBA Explorateur de fichiers passe de l'un \xC3\xA0 l'autre.");

    d.heading(2, "L'arbre du projet", "arbre-lot8");
    d.indexTerm("arbre");
    d.indexTerm("filtrer l'arbre");
    d.indexTerm("\xC3\xA9pingler");
    // 1.11, decision 12 : la phrase d'introduction est le resume de la page dans le
    // centre d'aide (CenterSources : le premier paragraphe), que le tutoriel deduit lit.
    d.paragraph("L'arbre, \xC3\xA0 gauche, montre tout le projet en quatre domaines (API, IHM, Simulation, Versions) : on le "
                "filtre, on y \xC3\xA9pingle ce qu'on ouvre souvent, et ses pastilles disent ce qui demande ton attention.");
    d.bullet("Le filtre, en haut (Ctrl+Maj+F) : l'arbre ne garde que ce qui correspond, d\xC3\xA9plie ce qu'il faut et surligne "
             "les lettres trouv\xC3\xA9" "es ; il cherche aussi dans le contenu des dossiers (sections, types, vues, variables, "
             "recettes, alarmes, scripts). \xC3\x89" "chap l'efface.");
    d.bullet("Les outils sortent de l'arbre : une rang\xC3\xA9" "e de boutons sous le titre de chaque domaine (API : Statistiques ; "
             "IHM : \xC3\x89" "changes, Rechercher, Outil Modbus, G\xC3\xA9n\xC3\xA9rer, Compiler).");
    d.bullet("Les pastilles : les compteurs align\xC3\xA9s \xC3\xA0 droite ; en rouge, les expressions impossibles du dernier "
             "Compiler (sur Vues et sur le titre IHM) ; en orange, les mises \xC3\xA0 jour de biblioth\xC3\xA8que (Types d\xC3\xA9riv\xC3\xA9s, "
             "Blocs DFB, le titre API) ; un point orange sur ce qui a chang\xC3\xA9 depuis la derni\xC3\xA8re version ; l'\xC3\xA9tat de la "
             "simulation sur son titre.");
    d.bullet("\xC3\x89pingl\xC3\xA9s et R\xC3\xA9" "cents, en haut : clic droit \xE2\x80\xBA \xC3\x89pingler (ou l'\xC3\xA9pingle au survol d'une ligne) ; "
             "les derniers onglets ouverts. Les versions en bref : le travail en cours et les cinq derni\xC3\xA8res, puis "
             "\xC2\xAB Voir les N versions\xE2\x80\xA6 \xC2\xBB.");
    d.bullet("Le rail, \xC3\xA0 gauche de l'arbre : Tout, \xC3\x89pingl\xC3\xA9s, API, IHM, Simulation, Versions (un clic ne montre que ce "
             "domaine), puis Suivre l'onglet actif, Tout replier et la densit\xC3\xA9 (serr\xC3\xA9" "e ou large). Chaque domaine a sa "
             "couleur, prise du th\xC3\xA8me ; son titre reste coll\xC3\xA9 en haut quand on fait d\xC3\xA9" "filer.");
    d.bullet("En pied, la sant\xC3\xA9 du projet : la simulation, les expressions impossibles, les mises \xC3\xA0 jour, la version "
             "modifi\xC3\xA9" "e ; chaque morceau se clique. Au survol d'une ligne, sa carte : son chemin, ce qu'il faut en savoir, "
             "les gestes.");

    d.heading(2, "Les bandeaux du haut et du bas", "bandeaux");
    d.indexTerm("bandeau");
    d.indexTerm("notifications");
    d.indexTerm("journal des messages");
    d.indexTerm("barre d'\xC3\xA9tat");
    // 1.11, decision 12 : l'introduction, resume de la page dans le centre d'aide.
    d.paragraph("Le bandeau du haut porte le projet, sa version, Annuler et R\xC3\xA9tablir, la palette Aller \xC3\xA0 / Faire\xE2\x80\xA6, "
                "la simulation et les notifications ; la barre d'\xC3\xA9tat, en bas, dit le dernier message, ce qui est choisi, "
                "les erreurs et l'enregistrement.");
    d.bullet("En haut, \xC3\xA0 gauche : le projet (son nom, DEV / RELEASE, le point \xC2\xAB modifi\xC3\xA9 \xC2\xBB, le bouton Enregistrer ; "
             "son menu : les projets r\xC3\xA9" "cents, Nouveau, Ouvrir, Enregistrer sous, Fermer) ; la version et ses modifications "
             "(Cr\xC3\xA9" "er un essai, les versions, ce qui a chang\xC3\xA9) ; Annuler et R\xC3\xA9tablir, avec la liste des dix "
             "derni\xC3\xA8res actions (un clic y revient ; l'historique du projet est en bas de cette liste, Ctrl+H).");
    d.bullet("Au centre, la palette Aller \xC3\xA0 / Faire\xE2\x80\xA6 (Ctrl+K) : elle cherche dans le projet ; avec \xC2\xAB > \xC2\xBB devant le "
             "texte, elle cherche les commandes (> compiler, > th\xC3\xA8me nord, > nouvelle variable) ; vide, elle montre les "
             "derniers choix.");
    d.bullet("\xC3\x80 droite : la simulation (Simuler, Arr\xC3\xAAter, Un cycle, l'\xC3\xA9tat en couleur, le cycle et la courbe du temps "
             "de cycle ; un clic sur l'\xC3\xA9tat ouvre la Vue d'ensemble) ; la cloche des notifications (les expressions "
             "impossibles du dernier Compiler, les mises \xC3\xA0 jour de biblioth\xC3\xA8que, un export termin\xC3\xA9, ce que la "
             "simulation signale ; chacune avec son bouton ; Tout marquer comme lu) ; les t\xC3\xA2" "ches de fond (une jauge, le "
             "d\xC3\xA9tail, Annuler) ; Vers Control Expert et la cible ; D\xC3\xA9poser un fichier ; Affichage et Aide.");
    d.bullet("En bas, la barre d'\xC3\xA9tat : le dernier message (un clic ouvre le journal des 50 derniers, avec l'heure et "
             "\xC2\xAB Aller \xC3\xA0 \xC2\xBB) ; ce qui est choisi (une variable : son type, son adresse, qui la lit, les sections qui la "
             "nomment ; une section ; une vue) ; les erreurs et les avertissements (un clic ouvre la liste) ; la simulation ; "
             "la cible ; le zoom de l'onglet (75 \xC3\xA0 150 %) ; le th\xC3\xA8me (les derniers utilis\xC3\xA9s) ; l'enregistrement "
             "(\xC2\xAB Modifi\xC3\xA9 : Ctrl+S \xC2\xBB se clique) ; les raccourcis du moment.");
    // ---- fin Lot API 8 : l'explorateur, l'arbre, les bandeaux ----
    // ---- fin Lot API 8 : didacticiels et aide ----

    // Lot API 2 : la barre du haut, l'arbre de l'API, ses onglets. Le lot macros 1
    // suit (son detail reste ci-dessous).
    d.heading(2, "Nouveaut\xC3\xA9s : la barre du haut, l'arbre de l'API, ses onglets", "nouveautes");
    d.indexTerm("barre du haut");
    d.indexTerm("tableau de bord");
    d.indexTerm("configuration");
    d.indexTerm("pastilles");
    d.bullet("La barre du haut est rang\xC3\xA9" "e en groupes nomm\xC3\xA9s : le projet (son nom, son \xC3\xA9tat, le point "
             "orange s'il reste des modifications ; un clic ouvre le menu Projet : ouvrir, enregistrer, importer le .XHW, "
             "r\xC3\xA9importer, l'\xC3\xA9tat, d\xC3\xA9verrouiller, Vers Control Expert, le menu principal) ; Annuler, "
             "R\xC3\xA9tablir, Historique ; + Nouveau (une section, une unit\xC3\xA9, un bloc, un type, une variable, un rack, "
             "un module, une macro) ; Aller \xC3\xA0\xE2\x80\xA6 ; la simulation (Simuler, Arr\xC3\xAAter, Un cycle, son "
             "\xC3\xA9tat et son num\xC3\xA9ro de cycle) ; Vers Control Expert ; Affichage et Aide. Les raccourcis ne changent pas.");
    d.bullet("Les quatre onglets fixes du centre (IO Mapping, Task Configuration, Communication, Layout) sont partis : un "
             "clic sur Configuration dans l'arbre ouvre le rack et ses propri\xC3\xA9t\xC3\xA9s, T\xC3\xA2" "ches ouvre les "
             "t\xC3\xA2" "ches et ce qu'elles ex\xC3\xA9" "cutent ; le choix des panneaux est dans Affichage.");
    d.bullet("Le projet s'ouvre sur le tableau de bord de l'API (un clic sur API dans l'arbre y revient) : l'automate, les entr\xC3\xA9" "es de la t\xC3\xA2" "che, "
             "les types, les variables, et \xC2\xAB \xC3\x80 regarder \xC2\xBB - ce qui a une version plus r\xC3\xA9" "cente "
             "dans la biblioth\xC3\xA8que, les variables que le programme n'utilise pas, les lectures avant l'\xC3\xA9" "criture "
             "dans l'ordre de la t\xC3\xA2" "che - chacun avec le bouton qui le r\xC3\xA8gle.");
    d.bullet("L'arbre de l'API est en fran\xC3\xA7" "ais : Types d\xC3\xA9riv\xC3\xA9s, Blocs DFB, Unit\xC3\xA9s de programme, "
             "T\xC3\xA2" "ches, Tables d'animation ; l'ordre d'ex\xC3\xA9" "cution compte ses entr\xC3\xA9" "es et ses sections.");
    d.bullet("L'onglet Macros : chaque macro a le glyphe de sa famille et sept pastilles - lit le classeur, lit un CSV, "
             "encha\xC3\xAEne d'autres macros, importe de la biblioth\xC3\xA8que, \xC3\xA9" "crit des sections, cr\xC3\xA9" "e "
             "des variables, \xC3\xA9" "crit un fichier. La l\xC3\xA9gende, sous la liste : un clic sur une pastille ne garde "
             "que les macros qui l'ont. La fiche le dit en phrases (Ce qu'elle touche).");
    d.bullet("La simulation dans la barre : Simuler (F5 ; F9 ouvre Simulation \xE2\x80\xBA Vue d'ensemble), Arr\xC3\xAAter (Maj+F5), Un cycle ; "   // Lot API 8 : didacticiels et aide
             "l'\xC3\xA9tat se lit en clair avec le num\xC3\xA9ro du cycle. Arr\xC3\xAAt\xC3\xA9" "e sur un d\xC3\xA9" "faut : "
             "l'infobulle de l'\xC3\xA9tat et la barre d'\xC3\xA9tat disent pourquoi (un bloc que le simulateur ne "
             "conna\xC3\xAEt pas, une division par z\xC3\xA9ro...).");

    // Lot macros 1 : ce qui est nouveau, et ou le trouver. Le lot 21 suit, en
    // une ligne (son detail est dans l'aide de l'IHM).
    // 1.11, decision 12 : le titre sans nom de chantier (l'ancre reste).
    d.heading(2, "Nouveaut\xC3\xA9s : l'onglet Macros, son formulaire, les onglets de l'aide", "nouveautes-macros");
    d.indexTerm("nouveaut\xC3\xA9s");
    d.indexTerm("macros");
    d.indexTerm("formulaire");
    d.indexTerm("explorateur de fichiers");
    d.indexTerm("Ctrl+V");
    d.indexTerm("dossiers");
    d.indexTerm("onglets de l'aide");
    d.paragraph("Le d\xC3\xA9tail de chaque macro est dans l'onglet Macros de cette aide (en haut) : ce qu'elle lit, ce "
                "qu'elle produit, les macros qu'elle encha\xC3\xAEne et chacune de ses questions.");
    d.bullet("L'onglet Macros (le bouton Macros de la barre, ou un double-clic sur une macro dans l'arbre API > Macros) : "
             "les macros rang\xC3\xA9" "es en dossiers, comme les fonctions IHM dans l'IHM - un dossier ne change aucun nom, "
             "RunMacro trouve une macro o\xC3\xB9 qu'elle soit. Rechercher, Favorites, R\xC3\xA9" "centes ; cr\xC3\xA9" "er une "
             "macro depuis un mod\xC3\xA8le, un dossier ; renommer (F2 : les RunMacro qui l'appellent suivent), dupliquer, "
             "supprimer (Suppr : elle va dans la corbeille, Restaurer la remet) ; glisser une macro ou un dossier dans un autre.");
    d.bullet("La fiche d'une macro : ce qu'elle lit (les onglets du classeur), ce qu'elle produit, ses \xC3\xA9tapes (les "
             "macros qu'elle lance) et ce qu'elle va te demander. Lancer (Entr\xC3\xA9" "e ou double-clic) ouvre son formulaire.");
    d.bullet("Le formulaire : chaque question a son champ. Un fichier se choisit avec le bouton \xE2\x80\xA6 (l'explorateur "
             "de fichiers), se colle (Ctrl+V d'un fichier copi\xC3\xA9 dans l'Explorateur) ou se glisse sur le champ ; une "
             "t\xC3\xA2" "che, une section, un bloc se choisissent dans ce que le projet contient ; oui / non est un "
             "interrupteur ; un choix, des boutons. L'aper\xC3\xA7u se refait \xC3\xA0 chaque r\xC3\xA9ponse : la macro tourne "
             "pour de vrai et d\xC3\xA9" "fait tout. Appliquer pose une seule commande : un Ctrl+Z reprend tout.");
    d.bullet("Les 31 macros de libs/Macros ont \xC3\xA9t\xC3\xA9 repens\xC3\xA9" "es pour ce formulaire : des libell\xC3\xA9s avec "
             "accents, des groupes, les r\xC3\xA9glages avanc\xC3\xA9s repli\xC3\xA9s, une aide courte sous chaque champ ; leur "
             "code ne change pas.");
    d.bullet("L'aide a quatre onglets : Aide (cette page), Macros, Blocs DFB / DDT et IHM. F1 ouvre directement le bon : "
             "sur une macro, sa page dans Macros ; sur un bloc ou un type, sa page dans Blocs DFB / DDT.");
    d.paragraph("Aussi : les versions du projet (Ctrl+Alt+S), le didacticiel en parcours, les dossiers de l'IHM "
                "(le d\xC3\xA9tail est dans l'onglet IHM de cette aide) ; coller depuis Excel, les mod\xC3\xA8les "
                "de vues, Aller \xC3\xA0\xE2\x80\xA6 (Ctrl+K) ; Ctrl+Z et Ctrl+Y partout, l'Historique (Ctrl+H).");

    d.heading(2, "Importer un fichier", "importer");
    d.indexTerm("import");
    d.paragraph("Les macros d'import lisent un CSV, un .xlsx ou un .xlsm. Le "
                "contenu compte, pas l'extension : un classeur et son export CSV "
                "donnent le m\xC3\xAAme r\xC3\xA9sultat.");
    d.bullet("La ligne d'en-t\xC3\xAAte est trouv\xC3\xA9" "e par ses COLONNES, pas par son "
             "num\xC3\xA9ro. Des lignes de titre au-dessus ne g\xC3\xAAnent pas.");
    d.bullet("Les colonnes sont reconnues par leur nom, sans tenir compte de la "
             "casse, des accents ni des espaces.");
    d.bullet("L'ordre des colonnes est libre, et celles qu'on ne conna\xC3\xAEt pas sont "
             "ignor\xC3\xA9" "es sans bruit.");
    d.bullet("Une ligne vide termine le tableau : la note qu'une feuille porte "
             "sous ses donn\xC3\xA9" "es n'est pas lue comme une donn\xC3\xA9" "e.");

    d.heading(2, "R\xC3\xA9importer", "reimporter");
    d.indexTerm("reimport");
    d.paragraph("Un second import MET \xC3\x80 JOUR ce qui existe et n'ajoute que ce qui "
                "manque. Chaque \xC3\xA9" "cart est signal\xC3\xA9 avant d'\xC3\xAAtre appliqu\xC3\xA9, et tu "
                "peux refuser de remplacer une valeur tout en continuant le "
                "reste de l'import.");

    d.heading(2, "L'ordre des sections", "ordre");
    d.indexTerm("cycle");
    d.indexTerm("TC");
    d.indexTerm("TM");
    d.indexTerm("TA");
    d.paragraph("Ce n'est pas une question de go\xC3\xBBt. Une commande vient de l'IHM et "
                "doit \xC3\xAAtre lue AVANT que les blocs ne s'ex\xC3\xA9" "cutent ; une "
                "information part vers l'IHM et doit \xC3\xAAtre \xC3\xA9" "crite APR\xC3\x88S. "
                "L'inverser fait remonter ce que le bloc avait calcul\xC3\xA9 au cycle "
                "PR\xC3\x89" "C\xC3\x89" "DENT - un cycle de retard que personne ne cherche, parce "
                "que personne ne le soup\xC3\xA7onne.");
    d.tableRow({"Rang", "Type", "Section", "Quand"}, true);
    d.tableRow({"0", "TC", sectionFor(ReportKind::Command),
                "commandes venant de l'IHM, lues en premier"});
    d.tableRow({"1", "", "(tes blocs)", "les \xC3\xA9quipements calculent"});
    d.tableRow({"2", "TA", sectionFor(ReportKind::Alarm),
                "alarmes, par DFB_ALM_MANAGER"});
    d.tableRow({"3", "TM", sectionFor(ReportKind::Measure),
                "informations, \xC3\xA9" "crites en dernier"});

    d.heading(2, "Les alarmes", "alarmes");
    d.indexTerm("alarme");
    d.indexTerm("DFB_ALM_MANAGER");
    d.paragraph("Une alarme ne se recopie pas dans un mot : elle passe par "
                "DFB_ALM_MANAGER, qui l'horodate et tient l'historique. Une "
                "alarme \xC3\xA9" "crite \xC3\xA0 la main dans un %MX perd sa date d'apparition, "
                "et c'est pr\xC3\xA9" "cis\xC3\xA9ment ce qu'on vient chercher six mois plus tard.");

    d.separator();
    d.heading(1, "Formats de fichier", "formats");

    for (const auto& f : formats()) {
        d.heading(2, f.title, "fmt-" + f.id);
        d.indexTerm(f.title);
        d.paragraph(f.purpose);
        d.paragraph("Identifiant : " + f.id + ".  Colonnes rep\xC3\xA9rant l'en-t\xC3\xAAte : "
                    + [&] {
                          std::string s;
                          for (const auto& a : f.anchors())
                              s += (s.empty() ? "" : ", ") + a;
                          return s;
                      }() + ".");

        d.heading(3, "Colonnes", "col-" + f.id);
        d.tableRow({"Colonne", "Obligatoire", "Exemple", "\xC3\x80 quoi elle sert"}, true);
        for (const auto& c : f.columns)
            d.tableRow({c.name, c.required ? "oui" : "", c.example, c.help});

        d.heading(3, "Fichier d'exemple", "ex-" + f.id);
        d.paragraph("Trois lignes : les titres, une ligne d'aide que l'import "
                    "saute, et une ligne remplie. \xC2\xAB Enregistrer les fichiers "
                    "d'exemple \xC2\xBB, en haut de la page Importer un fichier, les "
                    "\xC3\xA9" "crit tous en CSV, en UTF-8 avec BOM (Excel lit les "
                    "accents) ; remplace la derni\xC3\xA8re ligne par tes donn\xC3\xA9" "es.");
        d.code(f.exampleCsv(';'));
    }

    return d;
}

} // namespace ui
