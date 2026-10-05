#include "Workbook.hpp"

#include "../import/XmlReader.hpp"

#include <algorithm>
#include <cstring>
#include <map>

#include "../../third_party/miniz.h"

namespace xls {

namespace {

// ---------------------------------------------------------------------------
//  L'archive. Ouverte une fois, gardee le temps de la lecture : ouvrir et
//  refermer pour chacun des quatre fichiers relit le repertoire central a
//  chaque fois.
// ---------------------------------------------------------------------------
class Archive {
public:
    explicit Archive(const std::string& path) {
        std::memset(&zip_, 0, sizeof zip_);
        ok_ = mz_zip_reader_init_file(&zip_, path.c_str(), 0) != 0;
    }
    ~Archive() { if (ok_) mz_zip_reader_end(&zip_); }
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] bool has(const char* entry) {
        if (!ok_) return false;
        return mz_zip_reader_locate_file(&zip_, entry, nullptr, 0) >= 0;
    }

    // Le contenu d'une entree, decompresse. Chaine vide si absente : un .xlsx
    // valide peut ne pas avoir de sharedStrings.xml quand rien n'est du texte.
    [[nodiscard]] std::string read(const char* entry) {
        if (!ok_) return {};
        std::size_t size = 0;
        void* raw = mz_zip_reader_extract_file_to_heap(&zip_, entry, &size, 0);
        if (!raw) return {};
        std::string out(static_cast<const char*>(raw), size);
        mz_free(raw);
        return out;
    }

private:
    mz_zip_archive zip_{};
    bool ok_{false};
};

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// Le nom local d'un element : "x:row" -> "row". Les classeurs de Google Sheets
// et de LibreOffice prefixent parfois leurs elements, Excel non - et une
// comparaison sur le nom complet ne marcherait que chez l'un des trois.
std::string_view localName(std::string_view name) {
    const auto colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

// ---------------------------------------------------------------------------
//  Les chaines partagees.
//
//  UNE SEULE PASSE, ET LA TAILLE RESERVEE D'AVANCE. L'attribut uniqueCount dit
//  combien il y en a ; sans lui, un classeur de cent mille chaines fait
//  reallouer le vecteur dix-sept fois.
//
//  Une chaine peut etre coupee en plusieurs <t> - Excel le fait des qu'une
//  partie du texte est mise en forme differemment. Les concatener est le seul
//  moyen de retrouver "Pompe gavage A" ecrit avec "A" en gras.
// ---------------------------------------------------------------------------
std::vector<std::string> readSharedStrings(Archive& zip) {
    std::vector<std::string> out;
    const auto xml = zip.read("xl/sharedStrings.xml");
    if (xml.empty()) return out;

    importer::XmlReader r(xml);
    bool inItem = false;
    std::string current;

    while (auto ev = r.next()) {
        if (*ev == importer::XmlEvent::EndOfDocument) break;

        if (*ev == importer::XmlEvent::StartElement) {
            const auto name = localName(r.name());
            if (name == "sst") {
                const auto count = r.attr("uniqueCount", r.attr("count", "0"));
                const auto n = static_cast<std::size_t>(std::atoll(count.c_str()));
                if (n > 0 && n < 5'000'000) out.reserve(n);
            } else if (name == "si") {
                inItem = true;
                current.clear();
            }
        } else if (*ev == importer::XmlEvent::Text && inItem) {
            // DECODE : r.text() est le texte brut du XML. "Pompe A &amp; B"
            // restait tel quel, et un accent ecrit &#233; aussi.
            current += importer::XmlReader::decodeEntities(r.text());
        } else if (*ev == importer::XmlEvent::EndElement) {
            if (localName(r.name()) == "si") {
                out.push_back(current);
                inItem = false;
            }
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
std::size_t columnOf(std::string_view ref) {
    // "BC12" -> les lettres seulement, en base 26 SANS zero : A vaut 1, Z vaut
    // 26, AA vaut 27. Traiter A comme zero donne AA = 0 et decale toute la
    // feuille d'une colonne a partir de la vingt-septieme.
    std::size_t col = 0;
    for (char ch : ref) {
        const auto c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z') col = col * 26 + (c - 'a' + 1);
        else break;
    }
    return col == 0 ? 0 : col - 1;
}

std::size_t rowOf(std::string_view ref) {
    std::size_t at = 0;
    while (at < ref.size() && !std::isdigit(static_cast<unsigned char>(ref[at]))) ++at;
    std::size_t row = 0;
    while (at < ref.size() && std::isdigit(static_cast<unsigned char>(ref[at]))) {
        row = row * 10 + static_cast<std::size_t>(ref[at] - '0');
        ++at;
    }
    return row == 0 ? 0 : row - 1;
}

// ---------------------------------------------------------------------------
WorkbookInfo inspect(const std::string& path) {
    WorkbookInfo info;
    Archive zip(path);
    if (!zip.ok()) {
        info.warnings.push_back("ce fichier n'est pas une archive lisible : un .xlsx "
                                "et un .xlsm sont des archives ZIP, un .xls ne l'est pas");
        return info;
    }

    info.hasMacros = zip.has("xl/vbaProject.bin");

    // rId -> chemin reel. L'ordre des feuilles dans workbook.xml n'est PAS
    // l'ordre des fichiers sheetN.xml : supposer que la premiere feuille est
    // sheet1.xml marche sur un classeur neuf et faux sur un classeur reorganise.
    std::map<std::string, std::string> cibles;
    {
        const auto rels = zip.read("xl/_rels/workbook.xml.rels");
        importer::XmlReader r(rels);
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev == importer::XmlEvent::StartElement
                && localName(r.name()) == "Relationship") {
                auto target = r.attr("Target");
                // UNE CIBLE ABSOLUE PART DE LA RACINE DU PAQUET ("/xl/worksheets/
                // sheet1.xml", ce qu'ecrivent openpyxl et d'autres outils) ; les
                // entrees de l'archive n'ont pas de '/' devant. Gardee telle quelle,
                // elle ne designait rien et la feuille paraissait vide.
                if (!target.empty() && target[0] == '/') target.erase(0, 1);
                else if (!target.empty()) target = "xl/" + target;
                if (target.rfind("xl//", 0) == 0) target.erase(2, 1);
                cibles[r.attr("Id")] = target;
            }
        }
    }

    const auto wb = zip.read("xl/workbook.xml");
    if (wb.empty()) {
        info.warnings.push_back("xl/workbook.xml est absent : l'archive s'ouvre mais "
                                "ce n'est pas un classeur");
        return info;
    }

    importer::XmlReader r(wb);
    while (auto ev = r.next()) {
        if (*ev == importer::XmlEvent::EndOfDocument) break;
        if (*ev != importer::XmlEvent::StartElement) continue;
        if (localName(r.name()) != "sheet") continue;

        SheetInfo s;
        s.name = r.attr("name");
        const auto etat = r.attr("state");
        s.hidden = (etat == "hidden" || etat == "veryHidden");

        // L'attribut porte le prefixe de l'espace de noms des relations, qui
        // n'est pas toujours "r". On prend celui qui finit par ":id".
        std::string rid = r.attr("r:id");
        if (rid.empty()) {
            for (const auto& a : r.attributes()) {
                if (a.name.size() >= 3 && a.name.substr(a.name.size() - 3) == ":id") {
                    rid = std::string(a.rawValue);
                    break;
                }
            }
        }
        if (const auto at = cibles.find(rid); at != cibles.end()) s.path = at->second;
        info.sheets.push_back(std::move(s));
    }

    // La table des chaines : sa taille dit tout de suite si le classeur est
    // gros, ce qui permet a l'appelant d'annoncer une attente.
    const auto sst = zip.read("xl/sharedStrings.xml");
    if (!sst.empty()) {
        importer::XmlReader rs(sst);
        while (auto ev = rs.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev == importer::XmlEvent::StartElement && localName(rs.name()) == "sst") {
                info.sharedStrings = static_cast<std::size_t>(
                    std::atoll(rs.attr("uniqueCount", rs.attr("count", "0")).c_str()));
                break;
            }
        }
    }
    return info;
}

// ---------------------------------------------------------------------------
std::vector<std::vector<std::string>>
parseSheet(std::string_view xml, const std::vector<std::string>& sharedStrings,
           std::size_t maxRows, bool* formulasSeen) {
    std::vector<std::vector<std::string>> lignes;
    std::vector<std::string> courante;
    std::size_t colonne = 0;
    std::string typeCellule, valeur;
    bool dansValeur = false, dansFormule = false;

    importer::XmlReader r(xml);
    while (auto ev = r.next()) {
        if (*ev == importer::XmlEvent::EndOfDocument) break;

        if (*ev == importer::XmlEvent::StartElement) {
            const auto name = localName(r.name());
            if (name == "row") {
                courante.clear();
            } else if (name == "c") {
                // LA POSITION VIENT DE L'ATTRIBUT r, PAS DE L'ORDRE D'ARRIVEE.
                // Une ligne ne porte que ses cellules non vides : empiler les
                // valeurs decale d'une colonne tout ce qui suit le premier trou,
                // sans rien casser d'apparent.
                colonne = columnOf(r.attr("r"));
                typeCellule = r.attr("t");
                valeur.clear();
            } else if (name == "v") {
                dansValeur = true;
            } else if (name == "f") {
                // On NOTE la formule et on ne la lit pas : ce qu'on veut est le
                // resultat, qui est dans <v> juste apres.
                dansFormule = true;
                if (formulasSeen) *formulasSeen = true;
            } else if (name == "t" && typeCellule == "inlineStr") {
                dansValeur = true;
            }
        } else if (*ev == importer::XmlEvent::Text) {
            if (dansValeur && !dansFormule) valeur += importer::XmlReader::decodeEntities(r.text());
        } else if (*ev == importer::XmlEvent::EndElement) {
            const auto name = localName(r.name());
            if (name == "v" || name == "t") {
                dansValeur = false;
            } else if (name == "f") {
                dansFormule = false;
            } else if (name == "c") {
                // "s" : la valeur est un INDEX dans la table commune. Une
                // cellule de texte ne contient pas son texte.
                std::string texte = valeur;
                if (typeCellule == "s") {
                    const auto idx = static_cast<std::size_t>(std::atoll(valeur.c_str()));
                    texte = idx < sharedStrings.size() ? sharedStrings[idx] : std::string{};
                }
                if (courante.size() <= colonne) courante.resize(colonne + 1);
                courante[colonne] = trim(texte);
            } else if (name == "row") {
                lignes.push_back(courante);
                if (lignes.size() >= maxRows) break;
            }
        }
    }
    return lignes;
}

const SheetInfo* chooseSheet(const std::vector<SheetInfo>& sheets,
                             std::string_view wanted) {
    if (sheets.empty()) return nullptr;

    if (wanted.empty()) {
        // LE PREMIER VISIBLE, pas le premier tout court. Une feuille masquee l'a
        // ete expres - c'est presque toujours une feuille de travail - et la
        // rendre par defaut donne un tableau que personne ne reconnait.
        for (const auto& s : sheets)
            if (!s.hidden) return &s;
        return &sheets.front();   // toutes masquees : mieux vaut celle-la que rien
    }

    for (const auto& s : sheets) {
        if (s.name.size() != wanted.size()) continue;
        if (std::equal(s.name.begin(), s.name.end(), wanted.begin(),
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a))
                               == std::tolower(static_cast<unsigned char>(b));
                       }))
            return &s;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
ReadResult read(const std::string& path, const ReadOptions& options) {
    ReadResult out;

    const auto info = inspect(path);
    out.warnings = info.warnings;
    if (info.sheets.empty()) return out;

    // Quelle feuille. Un nom qui ne correspond a rien est une erreur qui se
    // dit AVEC LA LISTE : "onglet introuvable" sans dire lesquels existent
    // oblige a rouvrir le classeur pour regarder.
    const SheetInfo* choisie = chooseSheet(info.sheets, options.sheet);
    {
        if (!choisie) {
            std::string liste;
            for (const auto& s : info.sheets)
                liste += (liste.empty() ? "" : ", ") + s.name + (s.hidden ? " (masque)" : "");
            out.warnings.push_back("aucun onglet nomme '" + options.sheet
                                   + "'. Le classeur contient : " + liste);
            return out;
        }
    }
    out.sheetUsed = choisie->name;

    Archive zip(path);
    if (!zip.ok()) return out;
    if (choisie->path.empty()) {
        out.warnings.push_back("l'onglet '" + choisie->name + "' n'a pas de fichier "
                               "associe dans l'archive");
        return out;
    }

    const auto chaines = readSharedStrings(zip);
    const auto feuille = zip.read(choisie->path.c_str());
    if (feuille.empty()) {
        out.warnings.push_back(choisie->path + " est vide ou illisible");
        return out;
    }

    bool formulesVues = false;
    const auto lignes = parseSheet(feuille, chaines, options.maxRows, &formulesVues);

    std::size_t largeur = 0;
    for (const auto& ligne : lignes) largeur = std::max(largeur, ligne.size());

    if (formulesVues) {
        out.warnings.push_back("des cellules sont calculees : la valeur lue est le "
                               "DERNIER RESULTAT enregistre par Excel. Un classeur "
                               "modifie puis ferme sans recalcul donne des valeurs "
                               "perimees.");
    }

    // ---- le meme tableau que pour un CSV -------------------------------
    //
    // On refabrique un CSV en memoire plutot que de dupliquer la logique de
    // Table : reperage de la ligne d'en-tete, colonnes par nom, lignes vides
    // ecartees, doublons signales. Tout cela est deja ecrit, deja teste, et
    // l'avoir en double serait l'avoir a corriger deux fois.
    //
    // Le separateur est le caractere d'unite (0x1F), qui ne peut pas apparaitre
    // dans une cellule Excel : une designation contenant un point-virgule
    // casserait un CSV, pas celui-ci.
    std::string csv;
    csv.reserve(lignes.size() * largeur * 12);
    for (const auto& ligne : lignes) {
        for (std::size_t c = 0; c < largeur; ++c) {
            if (c) csv.push_back('\x1F');
            if (c < ligne.size()) {
                // Un saut de ligne dans une cellule reste un saut de ligne, mais
                // il ne doit pas terminer la ligne du tableau.
                for (char ch : ligne[c]) csv.push_back(ch == '\n' ? ' ' : ch);
            }
        }
        csv.push_back('\n');
    }

    auto opts = options.table;
    opts.separator = '\x1F';
    // Une feuille Excel porte presque toujours une note sous ses donnees, et
    // cette note est dans la premiere colonne - donc dans une colonne d'ancrage.
    // Sans cette regle elle devient une ligne de donnees.
    if (opts.stopAfterBlankRows == 0) opts.stopAfterBlankRows = 1;
    out.table = project::Table::parse(csv, opts);
    for (const auto& w : out.table.warnings()) out.warnings.push_back(w);
    out.ok = true;
    return out;
}

} // namespace xls
