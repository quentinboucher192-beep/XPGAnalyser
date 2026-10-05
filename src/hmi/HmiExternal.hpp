// =============================================================================
//  hmi/HmiExternal.hpp — lire un fichier externe : apercu et donnees de tableau
// -----------------------------------------------------------------------------
//  Le gestionnaire montre ce que contient un fichier lie ; un objet Tableau
//  peut en afficher les lignes (propriete "source"). Chaque format rend la
//  meme chose : des en-tetes et des lignes quand le contenu est tabulaire, des
//  lignes de texte sinon, et un resume d'une ligne.
//
//    Excel    le lecteur .xlsx/.xlsm du projet (xls::read) ; l'onglet choisi
//    CSV      project::Table (separateur detecte)
//    TXT      les premieres lignes
//    JSON     valide ; un tableau d'objets devient un tableau (une colonne par cle)
//    XML      des elements freres de meme nom deviennent des lignes (attributs)
//    SQLite   lu directement dans le fichier (format documente, pas de pilote) :
//             les tables, et les lignes de celle qu'on choisit
//    Base externe  une chaine de connexion : on en montre les parametres ; la
//             connexion demande un pilote que l'outil n'embarque pas.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct ExternalData {
    std::vector<std::string>              headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string>              lines;      // un apercu en texte
    std::vector<std::string>              parts;      // onglets (Excel), tables (SQLite)
    std::string                           partUsed;
    std::size_t                           totalRows{0};
    std::string                           summary;
    std::string                           error;
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] ExternalData readExternal(ExternalKind, const std::string& path, std::string_view part = {},
                                        std::size_t maxRows = 200);

// Les deux lecteurs qui n'existaient pas dans le projet, exposes pour les tests.
struct JsonCheck { bool ok{false}; std::size_t line{0}, column{0}; std::string error; };
[[nodiscard]] JsonCheck checkJson(std::string_view text);

struct SqliteTable { std::string name; std::string sql; std::uint32_t rootPage{0}; };
struct SqliteFile {
    std::uint32_t pageSize{0}, pageCount{0};
    std::vector<SqliteTable> tables;
    std::string error;
};
[[nodiscard]] SqliteFile readSqliteSchema(const std::vector<std::uint8_t>& file);
// Les lignes d'une table (colonnes d'apres son CREATE TABLE), `maxRows` au plus.
[[nodiscard]] bool readSqliteRows(const std::vector<std::uint8_t>& file, const SqliteTable&, std::size_t maxRows,
                                  std::vector<std::string>& headers, std::vector<std::vector<std::string>>& rows,
                                  std::size_t* total = nullptr);

} // namespace hmi
