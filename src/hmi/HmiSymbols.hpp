// =============================================================================
//  hmi/HmiSymbols.hpp - les symboles reutilisables parametres (lot 10)
// -----------------------------------------------------------------------------
//  UN SYMBOLE EST UNE VUE DE ROLE "symbole" : un petit dessin fait d'objets
//  ordinaires (la carte d'une armoire : son cadre, son nom, sa bouteille, sa
//  pression, son bouton), qui declare des PARAMETRES comme une popup
//  (View::params : "Armoire := Armoires[0]; Nom := 'A'"). Ses expressions, ses
//  textes a trous, ses variables et ses actions les citent : Armoire.ana.PT1.mes,
//  {Nom}. La valeur apres := sert dans l'editeur du symbole, et quand une
//  instance ne donne pas l'argument.
//
//  UNE INSTANCE EST UN OBJET (Kind::SymbolInstance) POSE DANS UNE VUE. Elle
//  nomme son symbole ("symbol") et relie ses parametres ("params" :
//  "Armoire := Armoires[1]; Nom := 'B'"). Son cadre est celui du symbole, mis
//  a l'echelle ; elle tourne et se retourne comme un groupe. Ses actions a elle
//  (un clic ouvre la popup de l'armoire) partent d'un clic n'importe ou sur
//  elle, sauf sur un de ses objets qui a sa propre action pour ce geste.
//
//  L'EXPANSION. expandInstance() rend les objets du symbole POSES A LA PLACE DE
//  L'INSTANCE : cadres mis a l'echelle, retournes et tournes avec elle, noms
//  prefixes ("Carte_A.Titre"), identifiants derives (les memes d'une expansion
//  a l'autre), et chaque parametre REMPLACE PAR SON ARGUMENT la ou un nom se
//  lit : expressions, textes a trous, variables (ecrite, retour d'etat,
//  voyant...), conditions, plumes, cases de tableau, actions (cible, valeur,
//  condition, arguments passes a une popup) et scripts de la vue du symbole.
//  Armoire.ana.PT1.mes devient Armoires[1].ana.PT1.mes ; {Nom} devient B.
//
//  UNE INSTANCE SUIT DONC SON SYMBOLE : rien n'est recopie dans la vue, le
//  moteur (Runtime::viewOf) et l'editeur developpent les instances a chaque
//  fois. Modifier le symbole modifie toutes ses instances.
//
//  RIEN NE BOUCLE. Un symbole peut contenir des instances d'autres symboles ;
//  un symbole qui se contient lui-meme (directement ou non) s'arrete a
//  kMaxSymbolDepth niveaux, et Generer le dit.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

inline constexpr std::string_view kSymbolRole = "symbole";
inline constexpr int              kMaxSymbolDepth = 4;

// Les arguments d'une instance : (nom du parametre, texte de l'argument).
using SymbolArguments = std::vector<std::pair<std::string, std::string>>;

[[nodiscard]] bool isSymbolView(const View&) noexcept;
// Le symbole d'une instance (sa propriete "symbol") ; nul : introuvable, ou la
// vue nommee n'est pas un symbole.
[[nodiscard]] const View* symbolOf(const Project&, const Object& instance);
// Les symboles du projet, dans l'ordre du projet.
[[nodiscard]] std::vector<const View*> symbolsOf(const Project&);
// Les instances d'un symbole (par son nom), dans toutes les vues : (vue, objet).
[[nodiscard]] std::vector<std::pair<const View*, const Object*>> instancesOf(const Project&, std::string_view symbol);
// La vue a-t-elle des instances (elle-meme) ; ou, en comptant ce qu'elle
// emprunte (ecran modele, en-tete, pied) ?
[[nodiscard]] bool hasInstances(const View&) noexcept;
[[nodiscard]] bool usesSymbols(const Project&, const View&);
// `symbol` contient-il `other` (directement ou par un symbole qu'il contient) ?
[[nodiscard]] bool symbolContains(const Project&, const View& symbol, std::string_view other);

// Les arguments de l'instance, parametre par parametre du symbole : celui
// qu'elle donne, sinon la valeur par defaut ; un parametre sans l'un ni
// l'autre n'y est pas (son nom reste tel quel). 1.11.2 (decision 248) : un
// argument sans := (Voiture;50) est celui du parametre de meme rang, dans
// l'ordre du symbole ; un argument nomme garde la priorite ; un positionnel
// en trop est ignore.
[[nodiscard]] SymbolArguments symbolArguments(const View& symbol, const Object& instance);

// 1.11.2 (SYM, decision 240) : la section « Parametres du symbole » de l'inspecteur.
// Ce que `params` (le texte de l'instance) donne a chaque parametre du symbole, dans
// l'ordre du symbole ; vide : rien (la valeur par defaut s'applique). Les arguments
// nommes (Nom := 'B') vont a leur parametre ; les positionnels (Voiture;50) comme le
// moteur (symbolArguments) : le k-ieme au k-ieme parametre, s'il n'est pas nomme.
[[nodiscard]] std::vector<std::string> givenArguments(const View& symbol, std::string_view params);
// `params` apres la saisie de `value` pour le parametre `index`, en forme nommee :
// "Name := Voiture; Value := 50" (dans l'ordre du symbole, les vides omis) ; un argument
// nomme qui n'est pas un parametre du symbole est garde a la fin.
[[nodiscard]] std::string withArgument(const View& symbol, std::string_view params, std::size_t index, std::string_view value);

// ---- les remplacements (publics pour les tests) ----------------------------------
// Chaque nom de parametre, en tete d'un chemin, remplace par son argument (entre
// parentheses s'il n'est ni un chemin ni un litteral). Les chaines, les
// commentaires, les membres (x.Nom), les litteraux types (T#5s) et les noms
// d'arguments d'un appel (f(Nom := 1)) ne bougent pas. `code` : du ST (une
// affectation Nom := ... ecrit a travers le parametre).
[[nodiscard]] std::string substituteParams(std::string_view text, const SymbolArguments&, bool code = false);
// Les trous d'un texte a trous : {Nom}, {Armoire.ana.PT1.mes:0.0}.
[[nodiscard]] std::string substituteInTemplate(std::string_view text, const SymbolArguments&);
// Un trou qui n'est plus qu'une chaine ({'B'}) devient son texte (B).
[[nodiscard]] std::string resolveLiteralHoles(std::string_view text);
// L'inverse, pour creer un symbole : chaque chemin `path` (Armoires[0]) en
// tete d'un chemin devient `name` (Armoires[0].actif -> Armoire.actif).
[[nodiscard]] std::string replacePath(std::string_view text, std::string_view path, std::string_view name, bool code = false);

// Chaque texte d'un objet ou un nom se lit - expressions, textes a trous,
// variables, conditions, plumes, cases, actions, arguments - passe par `f`
// (texte, est_du_code) et prend ce qu'elle rend. Un texte a trous : chaque trou.
void rewriteNames(Object&, const std::function<std::string(std::string_view, bool code)>& f);
// Les chemins indexes lus par des objets ("Armoires[0]", une fois chacun, dans
// l'ordre ou on les rencontre) : ce que "Creer un symbole" propose en parametres.
[[nodiscard]] std::vector<std::string> indexedPaths(const std::vector<const Object*>&);
// "Armoire := Armoires[0]" pour chaque chemin indexe (un nom par racine : le
// singulier de la racine si elle finit par s, sinon la racine suivie de _1...).
[[nodiscard]] std::string suggestSymbolParams(const View&, const std::vector<Id>& selection);

// ---- l'expansion -------------------------------------------------------------------
// Un identifiant d'objet developpe : derive de l'instance et de l'objet du
// symbole, stable, et hors de la plage des identifiants du projet (bit haut).
[[nodiscard]] Id expandedId(Id instance, Id child) noexcept;

struct Expansion {
    std::vector<Object> objects;   // dans l'ordre de dessin ; les instances imbriquees deja developpees
    std::vector<Script> scripts;   // les scripts de vue du symbole (OnOpen, OnCycle, OnClose), relies
    std::vector<Action> actions;   // ses actions de vue, reliees
};
// Les objets du symbole a la place de l'instance. `depth` : le niveau
// d'imbrication (au-dela de kMaxSymbolDepth : rien). Symbole introuvable : vide.
[[nodiscard]] Expansion expandInstance(const Project&, const Object& instance, int depth = 0);
// La vue avec, apres chaque instance, ses objets developpes (l'instance reste :
// elle porte ses actions ; son dessin est celui de ses objets). Les scripts et
// les actions de vue des symboles s'ajoutent a ceux de la vue.
[[nodiscard]] View expandInstances(const Project&, const View&);

// ---- l'edition -----------------------------------------------------------------------
// Poser une instance du symbole `symbol` en (x, y) dans `view` (a sa taille,
// dans le calque actif). Rend son identifiant ; kNoId : pas un symbole.
Id placeSymbol(Project&, View& view, std::string_view symbol, double x, double y);
// "Creer un symbole" : les objets choisis (et ce qu'ils contiennent) partent
// dans une nouvelle vue de role symbole, `name`, a leur taille ; `params`
// ("Armoire := Armoires[0]; Nom := 'A'") deviennent ses parametres, et chaque
// chemin donne en valeur par defaut est remplace par le nom du parametre dans
// leurs expressions. Une instance les remplace, a leur place, sans argument :
// elle montre exactement ce qu'ils montraient. Faux (et `why`) : nom pris ou
// invalide, selection vide, ou `viewId` introuvable.
bool createSymbol(Project&, Id viewId, const std::vector<Id>& selection, const std::string& name,
                  const std::string& params, Id* instance = nullptr, std::string* why = nullptr);
// Renommer un symbole : ses instances suivent. Rend le nombre d'instances mises a jour.
std::size_t renameSymbol(Project&, std::string_view from, std::string_view to);

} // namespace hmi
