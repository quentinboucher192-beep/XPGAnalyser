// =============================================================================
//  hmi/HmiLanguages.hpp - l'IHM en plusieurs langues (lot 13)
// -----------------------------------------------------------------------------
//  CE QUI SE TRADUIT : ce que l'operateur lit - le texte des objets (et ses
//  trous : "Pompe {Pompe_Marche:en marche|a l'arret}" se traduit en entier, les
//  trous restent), les libelles d'un interrupteur, les choix d'une liste, d'un
//  selecteur, de boutons radio, les onglets, les boutons d'une barre de
//  navigation, les titres des cadres et des popups, les messages et les
//  consignes des alarmes. Une LISTE ("Petite vitesse;Grande vitesse;Arret") se
//  traduit choix par choix.
//
//  CE QUI NE SE TRADUIT PAS : les noms (vues, objets, variables), les valeurs
//  ecrites (un choix ecrit sa valeur, jamais son libelle traduit), les
//  expressions, les menus natifs de l'application.
//
//  EN MARCHE : la langue est celle de SYS.Language - le demarrage
//  (Configuration > Langues), puis un Selecteur de langue, l'action Changer de
//  langue, IHM_LANGUE('en') ou SYS.Language := 'de'. La vue montree se
//  traduit au dessin (translatedView) ; le moteur, lui, garde les textes
//  d'origine (les valeurs des choix ne changent pas avec la langue).
//
//  EXCEL : un classeur d'une ligne par texte - "Texte (fr)", une colonne par
//  langue ("English (en)"), "Ou" - s'exporte pour un traducteur et se relit.
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiModel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// Les proprietes d'objet qui se lisent (et se traduisent) ; celles qui sont des
// listes "a;b;c" (traduites choix par choix).
[[nodiscard]] bool isTranslatableKey(std::string_view key) noexcept;
[[nodiscard]] bool isTranslatableList(std::string_view key) noexcept;

struct TranslatableText {
    std::string              source;    // le texte d'origine
    std::vector<std::string> uses;      // "Vue_Commandes/Btn_Purge (text)", "alarme Defaut_Pompe (message)"
};
// Tous les textes a traduire du projet, chacun une fois, dans l'ordre du projet.
[[nodiscard]] std::vector<TranslatableText> translatableTexts(const Project&);

// Vrai : `code` est une langue du projet autre que la premiere - ses textes se
// traduisent (la langue du projet, un code inconnu : les textes tels quels).
[[nodiscard]] bool isTranslated(const Languages&, std::string_view code) noexcept;
// La traduction de `source` dans la langue `code` ; sans traduction (ou dans la
// langue du projet) : `source`. Une liste "a;b;c" se traduit choix par choix
// (les choix vides gardent leur place) ; une liste d'etats "0 = Arret | #4A5261"
// etat par etat (les valeurs et les couleurs restent). "pt-BR" sans traduction
// prend celle de "pt".
[[nodiscard]] std::string translateText(const Languages&, std::string_view code, std::string_view source);
[[nodiscard]] std::string translateList(const Languages&, std::string_view code, std::string_view list);
[[nodiscard]] std::string translateStates(const Languages&, std::string_view code, std::string_view states);
// La traduction posee pour `source` en `code` ("" : aucune).
[[nodiscard]] std::string translationOf(const Languages&, std::string_view code, std::string_view source);
// La vue a montrer dans la langue `code` : ses textes traduits (les proprietes
// fixes ; une propriete animee par une expression reste telle quelle).
[[nodiscard]] View translatedView(const View&, const Languages&, std::string_view code);

// "English" pour "en", "Deutsch" pour "de"... (le nom dans sa langue) ; un code
// inconnu : le code.
[[nodiscard]] std::string languageName(std::string_view code);
// Les langues connues (pour les proposer), dans l'ordre du catalogue.
[[nodiscard]] const std::vector<Language>& knownLanguages();
// "fr", "en", "pt-BR" : 2 ou 3 lettres, un sous-code facultatif.
[[nodiscard]] bool validLanguageCode(std::string_view code) noexcept;

struct LanguageCoverage {
    std::string code, name;
    std::size_t translated{0}, total{0};
};
// Pour chaque langue apres la premiere : combien de textes ont leur traduction.
[[nodiscard]] std::vector<LanguageCoverage> coverage(const Project&);

// ---- Excel -------------------------------------------------------------------------
// Le tableau a exporter : "Texte (fr)", une colonne par langue "English (en)", "Ou".
[[nodiscard]] ExportTable translationTable(const Project&);
// "English (en)" -> name "English", code "en".
[[nodiscard]] bool parseLanguageHeader(std::string_view header, std::string& name, std::string& code);
struct TranslationImport {
    std::size_t updated{0};          // traductions posees ou changees
    std::size_t cleared{0};          // traductions retirees (cellule videe)
    std::size_t unknown{0};          // lignes dont le texte d'origine n'est plus dans le projet (gardees)
    std::vector<std::string> languagesAdded;
    std::vector<std::string> warnings;
};
// Relit un tableau exporte (l'en-tete puis les lignes) dans `languages` : une
// langue nouvelle s'ajoute ; une cellule vide retire la traduction.
[[nodiscard]] TranslationImport importTranslations(const Project&, Languages& languages, const std::vector<std::string>& headers,
                                                   const std::vector<std::vector<std::string>>& rows);

// Les trous {...} d'un texte, dans l'ordre (pour verifier qu'une traduction les
// garde) : leur expression, sans le format ("{T:0.0}" -> "T").
[[nodiscard]] std::vector<std::string> templateHoles(std::string_view text);

// ---- le selecteur de langue : le dessin et le clic lisent la meme geometrie -------
struct LanguageChoice {
    std::string code, name;    // la langue du projet
    std::string label;         // ce qui s'ecrit : "EN", "English" ou "EN  English"
};
// Les langues du selecteur : celles de sa propriete "languages" qui sont dans le
// projet (dans son ordre), sinon toutes celles du projet.
[[nodiscard]] std::vector<LanguageChoice> languageChoices(const Object&, const Languages&);
struct LanguageSelectorLayout {
    bool             vertical{false};
    bool             toggle{false};      // une bascule : un seul bouton, la langue en cours
    std::vector<Box> buttons;            // un par choix (une bascule : un seul)
    double           fontSize{15};
};
[[nodiscard]] LanguageSelectorLayout languageSelectorLayout(const Object&, double w, double h, std::size_t count);
// "langue:N" (N : le rang du choix), "langue:suivante" (la bascule) ; "" : rien.
[[nodiscard]] std::string languageSelectorHit(const Object&, double w, double h, double lx, double ly, std::size_t count);

} // namespace hmi
