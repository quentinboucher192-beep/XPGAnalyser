// =============================================================================
//  ui/Icons.hpp - icons drawn from primitives
// -----------------------------------------------------------------------------
//  There is no image decoder in this build and no icon font, so every icon is
//  constructed from rectangles and lines at draw time. Three things follow from
//  that, and all three are advantages here:
//
//    * they scale to any row height without a second asset;
//    * they are tinted by the caller, so an icon can carry state (an unused DFB
//      is drawn in the warning colour, not in a second bitmap);
//    * there are no files to ship, install or get out of sync with the binary.
//
//  Each icon is drawn inside a square box and is designed on a 16-unit grid,
//  scaled to whatever rectangle it is given. They read at 16 px; below about
//  12 px the detail collapses and a filled shape would be better.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"
#include "../platform/Renderer.hpp"

#include <cstdint>

namespace ui {

enum class Icon : std::uint8_t {
    None = 0,
    // project structure
    Folder, FolderOpen, Project, Cpu, Rack, Module, Task, Section, Program,
    DerivedType, FunctionBlock, Library, Variable, LocatedVariable, Constant,
    AnimationTable,
    // diagnostics
    Info, Warning, Error, Ok,
    // commands
    Open, Save, Refresh, Analyze, Export, Print, Search, Settings, Play, Close,
    Document, Chart, Filter, Collapse, Expand,
    // running a simulation: the shapes everyone already knows
    Pause, Stop, StepOnce, Halt, Force,
    // Garder une page. L'etoile creuse dit "pas encore", la pleine "c'est
    // garde" : c'est le seul couple d'icones que tout le monde lit sans
    // legende, et une coche ne le remplace pas - une coche veut dire "fait".
    Star, StarFilled,
    // L'IHM : un ecran (le dossier IHM, une vue), une image (les ressources),
    // des calques empiles, et du code (scripts, compilation). A la FIN, comme
    // les autres ajouts : les numeros ne bougent pas.
    Screen, Image, Layers, Code,
    // Lot 4 de l'IHM : un cadenas (la securite, un objet verrouille en marche)
    // et une personne (les utilisateurs). A la FIN, toujours.
    Lock, User,
    // Lot 13 de l'IHM : un globe (les langues). A la FIN, toujours.
    Globe,
    // Lot 14 de l'IHM : deux boitiers relies (la communication), un ecran sur
    // son pied (le poste d'exploitation), une enveloppe (les notifications).
    Network, Station, Mail,
    // Lot 19 : annuler, retablir (deux fleches recourbees) et l'historique
    // (une horloge). A la FIN, toujours.
    Undo, Redo, History,
    // 1.8.0 : LES ICONES AU CHOIX de ce qui porte du code (core/CodeIcons.hpp) :
    // dix-huit, dans l'ordre du catalogue. A la FIN, toujours.
    CodeInit, CodeTor, CodeAna, CodeOutputs, CodeGrafcet, CodeActions, CodeConfig, CodeReset, CodeAlarm,
    CodeSafety, CodeHmi, CodeComm, CodeCalc, CodeTimer, CodeMatrix, CodeReports, CodeData, CodeDebug,
    // 1.11.23 : un clavier (les raccourcis des vues). A la FIN, toujours.
    Keyboard,
};

// 1.8.0 : l'icone du catalogue `index` (core::codeicons::info(index)) ; hors
// catalogue : Icon::None. Et dans l'autre sens : -1 si ce n'en est pas une.
[[nodiscard]] Icon codeIcon(int index) noexcept;
[[nodiscard]] int  codeIconIndex(Icon icon) noexcept;
// La couleur du catalogue de cette icone (sa teinte sur fond sombre).
[[nodiscard]] gfx::Color codeIconColor(int index) noexcept;

// Draws `icon` centred in `box`, tinted `color`. `box` is normally square; a
// non-square box is centred on its shorter side rather than stretched.
void drawIcon(gfx::IRenderer& r, Icon icon, const gfx::Rect& box, gfx::Color color);

} // namespace ui
