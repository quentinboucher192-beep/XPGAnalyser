// =============================================================================
//  project/LadderEdit.hpp - 1.51.0 : modifier un ladder (l'editeur graphique)
// -----------------------------------------------------------------------------
//  Les gestes de l'editeur, sur le modele (Ladder.hpp), sans ecran : poser un
//  contact, une bobine, un bloc, un fil ; changer une variable, un type ; effacer ;
//  tirer une branche en parallele (le OU) ; inserer ou supprimer une rangee, un
//  reseau ; deplacer. Chaque geste marque son reseau modifie : write() ne
//  reecrit que lui.
//
//  L'ANNULATION. L'editeur fait le geste sur une copie du schema, l'ecrit, et
//  pose le nouveau corps de la section par LadderEditCommand : UN geste, UN pas
//  de Ctrl+Z (la commande ne fusionne pas, contrairement a la frappe en ST).
// =============================================================================
#pragma once

#include "EditCommands.hpp"
#include "Ladder.hpp"

#include <string>
#include <vector>

namespace project::ladder {

    struct Pos {
        int network{ 0 }, row{ 0 }, col{ 0 };
        [[nodiscard]] bool operator==(const Pos&) const = default;
    };

    // La case est-elle dans le schema ?
    [[nodiscard]] bool valid(const Diagram& d, Pos p) noexcept;
    [[nodiscard]] const Cell* cellAt(const Diagram& d, Pos p) noexcept;
    // Le debut de l'element qui occupe cette case (une case couverte -> sa premiere colonne).
    [[nodiscard]] Pos anchorOf(const Diagram& d, Pos p) noexcept;

    // ---- poser (remplace un fil ou du vide ; refuse sur un bloc) ----
    bool placeContact(Diagram& d, Pos p, ContactType type, std::string name);
    bool placeCoil(Diagram& d, Pos p, CoilType type, std::string name);
    bool placeCompare(Diagram& d, Pos p, std::string expression, int width = 2);
    bool placeOperate(Diagram& d, Pos p, std::string statement, int width = 2);
    bool placeWire(Diagram& d, Pos p);
    bool placeJump(Diagram& d, Pos p, std::string label);
    bool placeReturn(Diagram& d, Pos p);
    bool placeLabel(Diagram& d, Pos p, std::string label);
    // Un bloc a cette case (b.x, b.y pris de p) ; sa taille suit ses broches.
    // Refuse s'il recouvre un element ou un autre bloc. Rend son indice, ou -1.
    int placeBlock(Diagram& d, Pos p, Block b);
    // La taille d'un bloc d'apres ses broches (largeur 2, une rangee par broche, EN/ENO compris).
    void fitBlock(Block& b);

    // ---- changer ----
    bool setName(Diagram& d, Pos p, std::string name);           // contact, bobine, saut, etiquette ; un bloc : son instance
    bool setText(Diagram& d, Pos p, std::string text);           // compare, operate
    bool setPin(Diagram& d, int network, int block, int pin, std::string effective);
    bool setPinInverted(Diagram& d, int network, int block, int pin, bool inverted);
    bool setContactType(Diagram& d, Pos p, ContactType t);
    bool setCoilType(Diagram& d, Pos p, CoilType t);
    bool cycleType(Diagram& d, Pos p);                           // le type suivant (contact, bobine)
    bool invert(Diagram& d, Pos p);                              // ouvert <-> ferme, bobine <-> inverse

    // ---- effacer ----
    // L'element (ou le bloc) de cette case. Entre deux elements, la case devient
    // un fil (le reseau reste ferme) ; sinon, du vide.
    bool erase(Diagram& d, Pos p);

    // ---- les liaisons verticales, les branches ----
    bool setLink(Diagram& d, int network, int row, int boundary, bool on);
    // Une branche en parallele autour des colonnes [p.col, p.col + width) de la
    // rangee p.row : sous elle, une rangee libre (inseree s'il le faut), un contact
    // ouvert sans variable a sa premiere colonne, des fils jusqu'au bout, les
    // deux liaisons verticales. Rend la case du nouveau contact.
    [[nodiscard]] Pos addParallel(Diagram& d, Pos p, int width = 1);

    // ---- les lignes (un « reseau » de Control Expert : des rangees reliees) ----
    // Les rangees [r0, r1] de la ligne qui contient `row` (reliees par des
    // liaisons verticales ou un bloc sur plusieurs rangees).
    void rungRows(const Diagram& d, int network, int row, int& r0, int& r1);
    // Une ligne neuve apres celle de `row` (une rangee vide pour les separer, puis
    // un fil du rail a une bobine sans variable) ; dans un reseau vide : sa
    // premiere rangee. Rend la rangee de la ligne neuve, -1 : impossible.
    int insertRung(Diagram& d, int network, int row);
    // Retire la ligne de `row` (ses rangees, ses blocs) et une rangee vide qui la separait.
    bool deleteRung(Diagram& d, int network, int row);

    // Un bloc d'un type : ses broches d'apres la bibliotheque (TON, ADD...) ou le
    // DFB du projet ; `instance` vide pour une fonction. Faux : type inconnu.
    bool makeBlock(const domain::Project* project, std::string_view type, std::string instance, bool enEno, Block& out);
    // Un nom d'instance libre : TON_1, TON_2... (variables du projet et blocs du schema).
    [[nodiscard]] std::string freeInstanceName(const domain::Project* project, const Diagram& d, std::string_view type);

    // ---- rangees et reseaux ----
    bool insertRow(Diagram& d, int network, int row);
    bool deleteRow(Diagram& d, int network, int row);
    // Un reseau neuf a `at` : une rangee, un fil du rail jusqu'a la bobine de la
    // derniere colonne (sans variable). Rend son indice.
    int insertNetwork(Diagram& d, int at);
    bool deleteNetwork(Diagram& d, int at);
    bool moveNetwork(Diagram& d, int from, int to);

    // Deplace un element (ou un bloc) : la cible doit etre libre (vide ou fil).
    bool moveElement(Diagram& d, Pos from, Pos to);

    // Le commentaire d'un reseau (le premier textBox ; cree au-dessus s'il manque, vide : retire).
    bool setNetworkComment(Diagram& d, int network, std::string text);
    [[nodiscard]] std::string networkComment(const Diagram& d, int network);

    // Les cases vides entre le debut de la rangee (le rail pour la rangee 0) et son
    // dernier element deviennent des fils.
    void autoWire(Diagram& d, int network, int row);

    // ---- l'annulation ----
    class LadderEditCommand final : public core::ICommand {
    public:
        LadderEditCommand(ProjectPtr project, domain::Index section, std::string body, std::string label);
        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override { return label_; }
        [[nodiscard]] domain::Index section() const noexcept { return section_; }
    private:
        ProjectPtr    project_;
        domain::Index section_;
        std::string   newBody_, oldBody_, label_;
        bool          captured_{ false };
    };

} // namespace project::ladder
