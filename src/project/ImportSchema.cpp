#include "ImportSchema.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace project {

namespace {

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x))
            == std::tolower(static_cast<unsigned char>(y));
    });
}

// Les colonnes que toutes les familles d'E/S partagent. Ecrites une fois : les
// recopier dans les quatre formats voudrait dire les corriger quatre fois.
std::vector<ColumnSpec> ioCommon() {
    return {
        {"Famille", false, "DI",
         "DI, DO, AI ou AO. Pr\xC3\xA9sente quand les quatre familles sont dans un seul "
         "onglet ; absente quand chaque famille a le sien, auquel cas c'est "
         "l'onglet qui la d\xC3\xA9signe."},
        {"Carte", true, "DI_R0S4",
         "Le nom de la carte. Toutes les voies d'une m\xC3\xAAme carte doivent porter le "
         "m\xC3\xAAme : c'est lui qui les regroupe."},
        {"Voie", false, "0",
         "Le num\xC3\xA9ro de voie sur la carte, \xC3\xA0 partir de z\xC3\xA9ro."},
        {"Designation", true, "Pr\xC3\xA9sence 24V armoire A",
         "Ce que l'op\xC3\xA9rateur lit. C'est la seule colonne que personne d'autre que "
         "toi ne peut remplir."},
        {"Repere", false, "S12",
         "Le rep\xC3\xA8re du sch\xC3\xA9ma \xC3\xA9lectrique, s'il y en a un."},
        {"Adresse", true, "%I0.4.0",
         "L'adresse physique. Elle n'est pas recalcul\xC3\xA9" "e \xC3\xA0 l'import : c'est celle "
         "du fichier qui fait foi."},
        {"Tableau", true, "CarteDI_R0S4",
         "Le nom du tableau ST qui portera les voies. L'import le cr\xC3\xA9" "e, "
         "dimensionn\xC3\xA9 au plus grand index rencontr\xC3\xA9."},
        {"Index", true, "0",
         "La place de cette voie dans le tableau. Deux voies au m\xC3\xAAme index sur le "
         "m\xC3\xAAme tableau sont refus\xC3\xA9" "es."},
        {"Commentaire", false, "",
         "Libre. Recopi\xC3\xA9 dans le commentaire de la variable."},
    };
}

std::vector<ColumnSpec> digitalExtra() {
    return {
        {"Inv", false, "N",
         "O inverse la voie \xC3\xA0 la lecture : un contact ferm\xC3\xA9 au repos se d\xC3\xA9" "clare "
         "ici plut\xC3\xB4t que dans le programme."},
        {"DebounceMs", false, "20",
         "L'anti-rebond, en millisecondes. Z\xC3\xA9ro le d\xC3\xA9sactive."},
        {"AlarmEn", false, "N",
         "O fait de cette voie une alarme. Elle passera alors par "
         "DFB_ALM_MANAGER et non par une simple recopie."},
        {"AlarmState", false, "1",
         "L'\xC3\xA9tat qui d\xC3\xA9" "clenche : 1 pour un contact qui se ferme, 0 pour un "
         "contact qui s'ouvre."},
        {"AlarmDelayMs", false, "0",
         "Le retard avant d\xC3\xA9" "clenchement. Il \xC3\xA9vite les alarmes fugitives."},
        {"Sev", false, "2",
         "La gravit\xC3\xA9, de 1 \xC3\xA0 4. Elle sert au tri dans l'historique."},
    };
}

std::vector<ColumnSpec> analogExtra() {
    return {
        {"Unite", false, "bars", "L'unit\xC3\xA9 physique, affich\xC3\xA9" "e telle quelle."},
        {"RawMin", true, "0", "La valeur brute rendue par la carte au minimum."},
        {"RawMax", true, "10000", "Et au maximum. \xC3\x89gale \xC3\xA0 RawMin, l'\xC3\xA9" "chelle est inutilisable et la voie "
                                  "est marqu\xC3\xA9" "e en d\xC3\xA9" "faut."},
        {"EngMin", true, "0", "Ce que RawMin repr\xC3\xA9sente en unit\xC3\xA9s physiques."},
        {"EngMax", true, "16", "Et RawMax."},
        {"Clamp", false, "O", "O borne la valeur \xC3\xA0 l'\xC3\xA9" "chelle. N laisse passer les d\xC3\xA9passements, qu'on "
                              "veut parfois voir."},
        {"DeadBand", false, "0", "Variation en dessous de laquelle on ne bouge pas."},
        {"FiltN", false, "1", "Ordre du filtre, de 0 \xC3\xA0 8."},
        {"RateMax", false, "0", "Variation maximale par seconde. Z\xC3\xA9ro la d\xC3\xA9sactive."},
    };
}

std::vector<ColumnSpec> thresholds() {
    std::vector<ColumnSpec> out;
    for (int k = 0; k < 4; ++k) {
        const auto n = std::to_string(k);
        out.push_back({"S" + n + "_En", false, k == 0 ? "O" : "N",
                       "Active le seuil " + n + "."});
        out.push_back({"S" + n + "_Dir", false, "H",
                       "H pour un seuil haut, B pour un seuil bas."});
        out.push_back({"S" + n + "_Val", false, "12",
                       "La valeur, en unit\xC3\xA9s physiques."});
        out.push_back({"S" + n + "_Hyst", false, "0.5",
                       "L'hyst\xC3\xA9r\xC3\xA9sis, appliqu\xC3\xA9" "e \xC3\x80 LA RETOMB\xC3\x89" "E seulement : un seuil qui se "
                       "d\xC3\xA9" "clenche \xC3\xA0 12 retombe \xC3\xA0 11,5."});
        out.push_back({"S" + n + "_DelayMs", false, "0", "Le retard de d\xC3\xA9" "clenchement."});
        out.push_back({"S" + n + "_Sev", false, "2", "La gravit\xC3\xA9, de 1 \xC3\xA0 4."});
    }
    return out;
}

std::vector<ColumnSpec> concat(std::vector<std::vector<ColumnSpec>> parts) {
    std::vector<ColumnSpec> out;
    for (auto& p : parts)
        out.insert(out.end(), std::make_move_iterator(p.begin()),
                   std::make_move_iterator(p.end()));
    return out;
}

const std::vector<ImportFormat>& allFormats() {
    static const std::vector<ImportFormat> list = [] {
        std::vector<ImportFormat> f;

        f.push_back({"es.di", "Entr\xC3\xA9" "es tout ou rien",
                     "Cr\xC3\xA9" "e le tableau de ST_IO_Dig, l'instance de DFB_IO_DIG de la "
                     "bonne taille, et la section qui l'appelle.",
                     concat({ioCommon(), digitalExtra()})});

        f.push_back({"es.do", "Sorties tout ou rien",
                     "Comme les entr\xC3\xA9" "es, avec le sens inverse : l'\xC3\xA9quipement "
                     "commande la voie.",
                     concat({ioCommon(), digitalExtra()})});

        f.push_back({"es.ai", "Entr\xC3\xA9" "es analogiques",
                     "Cr\xC3\xA9" "e le tableau de ST_IO_Ana avec son \xC3\xA9" "chelle et ses quatre "
                     "seuils, et l'instance de DFB_IO_ANA.",
                     concat({ioCommon(), analogExtra(), thresholds()})});

        f.push_back({"es.ao", "Sorties analogiques",
                     "M\xC3\xAAme chose, sans les seuils : une sortie ne se surveille pas, "
                     "elle se commande.",
                     concat({ioCommon(), analogExtra()})});

        f.push_back({"equipements", "\xC3\x89quipements",
                     "Cr\xC3\xA9" "e les variables - tableau ou variable seule - et les "
                     "instances de DFB pour chaque famille de la biblioth\xC3\xA8que.",
                     {
                         {"Nom", true, "Pompe gavage A",
                          "Le nom de l'\xC3\xA9quipement. Il sert de cl\xC3\xA9 partout ailleurs : deux "
                          "\xC3\xA9quipements ne peuvent donc pas le partager."},
                         {"Famille", true, "ST_EQ_Pump",
                          "Le type de la biblioth\xC3\xA8que. Il doit exister dans libs/ : un nom inconnu "
                          "arr\xC3\xAAte l'import, qui le nomme."},
                         {"Repere", false, "P1", "Le rep\xC3\xA8re du sch\xC3\xA9ma."},
                         {"Forme", false, "Tableau",
                          "Tableau ou Variable seule. Une DDT est l'un ou l'autre, jamais les deux "
                          ": Pompes[0] et Pompes sont deux choses diff\xC3\xA9rentes."},
                         {"Variable", true, "Pompes",
                          "Le nom ST. En forme Tableau, c'est le tableau ; en variable seule, "
                          "c'est la variable elle-m\xC3\xAAme."},
                         {"Index", false, "0",
                          "Obligatoire en forme Tableau, ignor\xC3\xA9 sinon. Deux \xC3\xA9quipements au m\xC3\xAAme "
                          "index sont refus\xC3\xA9s."},
                         {"Instance", false, "IO_Pompes",
                          "Le nom de l'instance du bloc, quand la famille en a un."},
                         {"Commentaire", false, "", "Libre."},
                     }});

        f.push_back({"reports", "Reports vers l'IHM",
                     "D\xC3\xA9" "clare les variables localis\xC3\xA9" "es %MW et %MX, et \xC3\xA9" "crit les "
                     "sections de recopie dans le bon ordre.",
                     {
                         {"Equipement", true, "Pompe gavage A",
                          "L'\xC3\xA9quipement concern\xC3\xA9. Il doit avoir \xC3\xA9t\xC3\xA9 import\xC3\xA9 avant, sinon son "
                          "chemin ST est inconnu."},
                         {"Parametre", true, "Fault",
                          "Le champ du bloc. Il doit exister dans la DDT : un nom inconnu arr\xC3\xAAte "
                          "l'import."},
                         {"Designation", true, "Pompe gavage A - d\xC3\xA9" "faut",
                          "Ce que l'op\xC3\xA9rateur lit sur l'IHM."},
                         {"Sens", true, "R",
                          "R : l'API \xC3\xA9" "crit, l'IHM lit. W : l'inverse. RW : les deux, ce qui "
                          "suppose que les deux c\xC3\xB4t\xC3\xA9s soient d'accord sur qui \xC3\xA9" "crit quand."},
                         {"Type", true, "TA",
                          "TM information, TA alarme, TC commande. Il d\xC3\xA9" "cide de la SECTION o\xC3\xB9 va "
                          "la recopie, et donc de son rang dans le cycle."},
                         {"Zone", true, "MW",
                          "MW pour un mot, MX pour un bit isol\xC3\xA9."},
                         {"Numero", true, "1000", "Le num\xC3\xA9ro de mot ou de bit."},
                         {"Bit", false, "1",
                          "Le rang du bit dans le mot, de 0 \xC3\xA0 15. Vide pour un mot entier."},
                         {"Unite", false, "bars", "Pour un report analogique."},
                         {"Mini", false, "0", "L'\xC3\xA9" "chelle basse affich\xC3\xA9" "e."},
                         {"Maxi", false, "16", "L'\xC3\xA9" "chelle haute."},
                         {"Resolution", false, "0.1", "Le pas d'affichage."},
                         {"Commentaire", false, "", "Libre."},
                     }});

        return f;
    }();
    return list;
}

} // namespace

// ---------------------------------------------------------------------------
std::vector<std::string> ImportFormat::anchors() const {
    // Les trois premieres obligatoires suffisent a reconnaitre une ligne
    // d'en-tete. En exiger davantage ferait echouer le reperage sur un fichier
    // pourtant valide a qui il manque une colonne facultative.
    std::vector<std::string> out;
    for (const auto& c : columns) {
        if (!c.required) continue;
        out.push_back(c.name);
        if (out.size() == 3) break;
    }
    return out;
}

const ColumnSpec* ImportFormat::find(std::string_view name) const {
    for (const auto& c : columns)
        if (sameName(c.name, name)) return &c;
    return nullptr;
}

std::vector<std::string> ImportFormat::requiredNames() const {
    std::vector<std::string> out;
    for (const auto& c : columns)
        if (c.required) out.push_back(c.name);
    return out;
}

std::string ImportFormat::exampleCsv(char separator) const {
    // TROIS LIGNES, ET LA DEUXIEME EST L'AIDE. Un fichier d'exemple qui ne
    // contient que des titres oblige a revenir a la documentation pour savoir ce
    // qu'on met dedans ; celui-ci se suffit a lui-meme, et la ligne d'aide est
    // une ligne de description que l'import saute.
    std::string titres, aides, exemple;
    for (const auto& c : columns) {
        if (!titres.empty()) { titres += separator; aides += separator; exemple += separator; }
        titres += c.name;

        std::string aide = c.required ? "obligatoire" : "facultatif";
        // Le separateur ne doit pas apparaitre dans une cellule non protegee.
        for (char ch : c.help) aide += (ch == separator || ch == '\n') ? ' ' : ch;
        aides += aide;

        exemple += c.example;
    }
    return titres + "\n" + aides + "\n" + exemple + "\n";
}

std::string ImportFormat::exampleCsvFile(char separator) const {
    return "\xEF\xBB\xBF" + exampleCsv(separator);   // le BOM : Excel lit les accents
}

std::vector<std::string> writeExampleCsvs(const std::string& dir, std::string* why) {
    namespace fs = std::filesystem;
    const auto utf8 = [](const fs::path& p) {
        const auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    };
    const fs::path base(std::u8string(dir.begin(), dir.end()));
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec || !fs::is_directory(base, ec)) {
        if (why) *why = "le dossier " + dir + " n'a pas pu se cr\xC3\xA9" "er";
        return {};
    }
    std::vector<std::string> out;
    for (const auto& f : allFormats()) {
        std::string name = f.id;
        std::replace(name.begin(), name.end(), '.', '-');
        const fs::path file = base / ("exemple-" + name + ".csv");
        const std::string bytes = f.exampleCsvFile(';');
        std::ofstream o(file, std::ios::binary | std::ios::trunc);
        o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!o) {
            if (why) *why = utf8(file) + " n'a pas pu s'\xC3\xA9" "crire";
            return {};
        }
        out.push_back(utf8(file));
    }
    return out;
}

const std::vector<ImportFormat>& formats() { return allFormats(); }

const ImportFormat* format(std::string_view id) {
    for (const auto& f : allFormats())
        if (sameName(f.id, id)) return &f;
    return nullptr;
}

// ---------------------------------------------------------------------------
ReportKind reportKindOf(std::string_view code) {
    if (sameName(code, "TC")) return ReportKind::Command;
    if (sameName(code, "TA")) return ReportKind::Alarm;
    return ReportKind::Measure;      // TM, et tout ce qu'on ne reconnait pas
}

std::string_view reportCode(ReportKind k) {
    switch (k) {
        case ReportKind::Command: return "TC";
        case ReportKind::Alarm:   return "TA";
        default:                  return "TM";
    }
}

int sectionOrder(ReportKind k) {
    // 0 : les commandes, LUES EN PREMIER. Elles viennent de l'IHM et doivent
    //     etre dans les variables avant que les blocs ne s'executent.
    // 1 : reserve aux blocs eux-memes, que l'import ne genere pas.
    // 2 : les alarmes, APRES les blocs, parce qu'elles dependent de ce que les
    //     blocs viennent de calculer.
    // 3 : les informations, en dernier. Rien n'en depend dans le cycle.
    switch (k) {
        case ReportKind::Command: return 0;
        case ReportKind::Alarm:   return 2;
        default:                  return 3;
    }
}

std::string sectionFor(ReportKind k) {
    switch (k) {
        case ReportKind::Command: return "REPORTS_TC_IN";
        case ReportKind::Alarm:   return "REPORTS_TA_ALM";
        default:                  return "REPORTS_TM_OUT";
    }
}

} // namespace project
