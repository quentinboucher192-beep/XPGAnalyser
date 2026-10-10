#include "UiDriver.hpp"

#include "../App.hpp"
#include "../NoveltyCenter.hpp"
#include "../TopBar.hpp"                 // tranche 24 (R111-12) : barre:<partie>
#include "../MacrosPane.hpp"             // 1.11.2 (R1112-3) : macro:<nom>
#include "../../help/TutorialScreen.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiObjectAlarmTree.hpp"
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiSimulation.hpp"         // le poste d'exploitation : sa vue en marche (StationScreen::pane)
#include "../screens/StationScreen.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/TabArea.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../ViewModels.hpp"
#include <algorithm>

#include <cctype>
#include <cstdlib>

namespace app {
namespace {

using ui::Key;
using ui::KeyMods;
using ui::MouseButton;

template <class F>
void walk(ui::Widget& w, F&& f) {
    f(w);
    for (const auto& c : w.children()) walk(*c, f);
}

bool shown(const ui::Widget& w) {
    for (const ui::Widget* p = &w; p; p = p->parent())
        if (!p->visible()) return false;
    return true;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "320,190" -> (320, 190)
bool parsePair(std::string_view s, float& x, float& y) {
    const auto comma = s.find(',');
    if (comma == std::string_view::npos) return false;
    const std::string a(s.substr(0, comma)), b(s.substr(comma + 1));
    char* end = nullptr;
    x = std::strtof(a.c_str(), &end);
    if (end == a.c_str()) return false;
    y = std::strtof(b.c_str(), &end);
    return end != b.c_str();
}

} // namespace

// ------------------------------------------------------------------ entrees -
void UiDriver::send(const ui::InputEvent& e) {
    ++sent_;
    // 1.10 (chantier P) : comme App::pumpEvents - la bulle des nouveautes prend
    // ses clics ; un clic sur un element marque retire son repere.
    if (noveltyCenter().handle(e) == ui::EventResult::Consumed) return;
    (void)app_.menus().HandleEvent(e);
}

void UiDriver::moveTo(gfx::Point p, KeyMods m) {
    send(ui::MouseMove{p, {p.x - mouse_.x, p.y - mouse_.y}, m});
    mouse_ = p;
}

void UiDriver::click(gfx::Point p, MouseButton b, int clicks, KeyMods m) {
    moveTo(p, m);
    send(ui::MouseDown{p, b, clicks, m});
    send(ui::MouseUp{p, b, m});
}

void UiDriver::press(gfx::Point p, KeyMods m) {
    moveTo(p, m);
    send(ui::MouseDown{p, MouseButton::Left, 1, m});
}

void UiDriver::release(KeyMods m) { send(ui::MouseUp{mouse_, MouseButton::Left, m}); }

void UiDriver::drag(gfx::Point a, gfx::Point b, KeyMods m, bool doRelease) {
    press(a, m);
    constexpr int kSteps = 8;
    for (int k = 1; k <= kSteps; ++k) {
        const float t = static_cast<float>(k) / kSteps;
        moveTo({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, m);
    }
    if (doRelease) send(ui::MouseUp{b, MouseButton::Left, m});
}

void UiDriver::typeInto(gfx::Point cell, const std::string& t) {
    click(cell, MouseButton::Left, 1, {});
    send(ui::KeyDown{Key::A, KeyMods{true, false, false, false}, false});
    send(ui::KeyUp{Key::A, KeyMods{true, false, false, false}});
    // Un texte vide VIDE le champ (champ 1 "") : la selection s'efface.
    if (t.empty()) key(Key::Delete);
    else text(t);
}

void UiDriver::key(Key k, KeyMods m) {
    send(ui::KeyDown{k, m, false});
    send(ui::KeyUp{k, m});
}

void UiDriver::text(const std::string& t) { send(ui::TextInput{t}); }

// ------------------------------------------------------------------ cibles --
ui::Widget* UiDriver::top() const {
    auto* menu = app_.menus().top();
    return menu ? menu->widgetRoot() : nullptr;
}

ui::Widget* UiDriver::currentPage() const {
    // Lot 14 : le poste d'exploitation - sa vue en marche est la page.
    if (auto* station = dynamic_cast<StationScreen*>(app_.menus().top())) return station->pane();
    auto* root = top();
    // Lot 7 : le centre est une TabArea (des groupes d'onglets) ; la page
    // courante est celle du groupe actif.
    auto* centre = root ? dynamic_cast<ui::TabArea*>(root->findById("analysis.centre")) : nullptr;
    return centre && centre->tabCount() ? centre->page(centre->currentIndex()) : nullptr;
}

HmiEditor* UiDriver::currentEditor() const { return dynamic_cast<HmiEditor*>(currentPage()); }

// Tire de ScriptRunner : "Ctrl+Z", "Maj+Tab", "Return", "F8"...
bool UiDriver::parseKey(const std::string& combo, Key& key, KeyMods& mods) {
    static const std::pair<const char*, Key> names[] = {
        {"escape", Key::Escape}, {"echap", Key::Escape}, {"return", Key::Return}, {"entree", Key::Return},
        {"tab", Key::Tab}, {"backspace", Key::Backspace}, {"retour", Key::Backspace}, {"delete", Key::Delete},
        {"suppr", Key::Delete}, {"insert", Key::Insert}, {"left", Key::Left}, {"gauche", Key::Left},
        {"right", Key::Right}, {"droite", Key::Right}, {"up", Key::Up}, {"haut", Key::Up}, {"down", Key::Down},
        {"bas", Key::Down}, {"home", Key::Home}, {"debut", Key::Home}, {"end", Key::End}, {"fin", Key::End},
        {"pageup", Key::PageUp}, {"pagedown", Key::PageDown}, {"space", Key::Space}, {"espace", Key::Space},
        {"a", Key::A}, {"c", Key::C}, {"f", Key::F}, {"n", Key::N}, {"o", Key::O}, {"p", Key::P}, {"s", Key::S},
        {"v", Key::V}, {"x", Key::X}, {"y", Key::Y}, {"z", Key::Z}, {"q", Key::Q}, {"h", Key::H}, {"k", Key::K}, {"f1", Key::F1}, {"f2", Key::F2},
        {"f3", Key::F3}, {"f5", Key::F5}, {"f11", Key::F11}, {"f12", Key::F12},
        {"w", Key::W}, {"f9", Key::F9}, {"1", Key::Num1}, {"5", Key::Num5},     // lot API 7
        {"f10", Key::F10},                                                     // Lot API 8 : Centre de simulation (F10 : la section suivante)
        {"f7", Key::F7},                                                       // 1.10 (chantier N) : Compiler l'IHM
        {"f8", Key::F8},                                                       // 1.10 : l'IHM demarrer / arreter (chantier L)
        {"d", Key::D},                                                         // 1.10.2 (chantier D) : Ctrl+D Dupliquer...
        {"j", Key::J},                                                         // 1.11.14 : Ctrl+J le panneau du bas
        // 1.11.23 : les raccourcis des vues - les autres lettres, chiffres, F4, F6
        {"b", Key::B}, {"e", Key::E}, {"g", Key::G}, {"i", Key::I}, {"m", Key::M}, {"r", Key::R}, {"t", Key::T},
        {"u", Key::U}, {"0", Key::Num0}, {"2", Key::Num2}, {"3", Key::Num3}, {"4", Key::Num4}, {"6", Key::Num6},
        {"7", Key::Num7}, {"8", Key::Num8}, {"9", Key::Num9}, {"f4", Key::F4}, {"f6", Key::F6},
        // 1.12.2 : les raccourcis des editeurs de code (Ctrl+L, Ctrl+/, Ctrl+], Ctrl+., Ctrl+,, Ctrl+-)
        {"l", Key::L}, {"/", Key::Slash}, {".", Key::Period}, {",", Key::Comma}, {"-", Key::Minus},
        {"]", Key::RightBracket}, {":", Key::Colon}, {"$", Key::Dollar}, {";", Key::Semicolon}};
    mods = {};
    std::string rest = lower(combo);
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string::npos || plus + 1 >= rest.size()) break;
        const std::string mod = rest.substr(0, plus);
        if (mod == "ctrl") mods.ctrl = true;
        else if (mod == "maj" || mod == "shift") mods.shift = true;
        else if (mod == "alt") mods.alt = true;
        else return false;
        rest = rest.substr(plus + 1);
    }
    for (const auto& [name, k] : names)
        if (rest == name) { key = k; return true; }
    return false;
}


std::optional<gfx::Rect> UiDriver::follow(std::string_view target) const {
    const bool was = quiet_;
    quiet_ = true;
    auto r = locate(target);
    quiet_ = was;
    return r;
}

bool UiDriver::locates(std::string_view kind) {
    return kind == "ecran" || kind == "bouton" || kind == "outil" || kind == "propriete" || kind == "biblio"
           || kind == "vue" || kind == "onglet" || kind == "variante" || kind == "sous-onglet" || kind == "arbre"
           || kind == "saisie" || kind == "alarme" || kind == "menu" || kind == "editeur" || kind == "champ"
           || kind == "ligne" || kind == "barre" || kind == "macro";
}

std::optional<gfx::Rect> UiDriver::locate(std::string_view target, std::string* why) const {
    auto miss = [&](std::string msg) -> std::optional<gfx::Rect> {
        if (why) *why = std::move(msg);
        return std::nullopt;
    };
    const auto colon = target.find(':');
    if (colon == std::string_view::npos) {
        // Une zone nommee. "variantes" : sous la bibliotheque (tant que la
        // bibliotheque n'a pas ses cartes de variantes, la bibliotheque entiere).
        if (target == "variantes")
            if (auto* editor = currentEditor(); editor && editor->palette().bounds().w > 0.f) return editor->palette().bounds();
        // Tranche 5. "resultats-compiler" : les resultats de Compiler - 1.11.21 : l'onglet Diagnostics
        // du panneau du bas (id "hmi.sorties.diagnostics" ; avant, le tableau sous l'editeur des
        // scripts) : sa premiere ligne, sinon le tableau entier.
        if (target == "resultats-compiler") {
            std::optional<gfx::Rect> r;
            if (auto* root = top())
                walk(*root, [&](ui::Widget& w) {
                    auto* t = dynamic_cast<ui::TableView*>(&w);
                    const std::string_view id = w.id();
                    if (r || !t || !shown(w) || id.size() < 12 || id.substr(id.size() - 12) != ".diagnostics") return;
                    gfx::Rect row{};
                    r = t->rowRect(0, row) ? row : t->bounds();
                });
            return r ? r : miss("pas de r\xC3\xA9sultats de Compiler ici");
        }
        // Tranche 5. "aide-saisie" : la liste de l'aide a la saisie ouverte (un champ ou l'editeur de code).
        if (target == "aide-saisie") {
            std::optional<gfx::Rect> r;
            if (auto* root = top())
                walk(*root, [&](ui::Widget& w) {
                    if (r || !shown(w)) return;
                    ui::InputText* f = dynamic_cast<ui::InputText*>(&w);
                    if (auto* g = dynamic_cast<ui::PropertyGrid*>(&w); !f && g) f = g->activeField();
                    if (f && f->suggestionsOpen()) {
                        const auto b = f->bounds(), eb = f->eventBounds();
                        if (eb.bottom() > b.bottom() + 0.5f) r = gfx::Rect{eb.x, b.bottom() + 1.f, eb.w, eb.bottom() - b.bottom() - 1.f};
                        else if (eb.y < b.y - 0.5f) r = gfx::Rect{eb.x, eb.y, eb.w, b.y - 1.f - eb.y};
                    } else if (auto* m = dynamic_cast<ui::MultiLineText*>(&w); m && m->completionOpen()) {
                        // Tranche 10 : la boite de sa liste (completionRect) ; pas encore dessinee :
                        // l'editeur et sa liste reunis, comme avant.
                        gfx::Rect box{};
                        r = m->completionRect({}, box) ? box : m->eventBounds();
                    }
                });
            return r ? r : miss("aide \xC3\xA0 la saisie ferm\xC3\xA9" "e");
        }
        // Tranche 5. "apercu-etats" : l'apercu des etats de la maquette n'existe pas encore ; l'objet
        // choisi dans la vue (son cadre : il montre son etat) en tient lieu. "alarmes-objet" : le
        // noeud Alarmes de l'objet choisi dans l'explorateur d'objets (deplie).
        if (target == "apercu-etats" || target == "alarmes-objet") {
            auto* editor = currentEditor();
            auto doc = app_.hmi();
            const auto* view = editor && doc ? doc->project.view(editor->viewId()) : nullptr;
            if (!view || editor->canvas().selection().empty()) return miss("aucun objet choisi (" + std::string(target) + ")");
            const auto id = editor->canvas().selection().back();
            const auto* o = view->object(id);
            if (!o) return miss("objet choisi introuvable");
            if (target == "apercu-etats") return locate("vue:" + o->name, why);
            if (!editor->objects().alarmNode(id)) return miss("l'objet ne porte pas d'alarme : " + o->name);
            if (!quiet_ && !editor->objects().alarmsOpen(id)) editor->objects().setAlarmsOpen(id, true);
            gfx::Rect r{};
            if (editor->objects().alarmRowRect(id, 0, r)) return r;
            if (!quiet_) editor->objects().reveal(id);
            return miss("alarmes de l'objet hors de vue : " + o->name);
        }
        return miss("zone nomm\xC3\xA9" "e inconnue de l'appli : " + std::string(target));
    }
    const auto kind = target.substr(0, colon);
    const std::string name(target.substr(colon + 1));
    float x = 0, y = 0;

    if (kind == "ecran") {
        if (!parsePair(name, x, y)) return miss("ecran:X,Y");
        return gfx::Rect{x - 1.f, y - 1.f, 2.f, 2.f};
    }
    if (kind == "bouton") {
        auto* root = top();
        if (!root) return miss("aucun \xC3\xA9" "cran");
        const bool prefix = !name.empty() && name.back() == '*';
        const std::string want = prefix ? name.substr(0, name.size() - 1) : name;
        ui::Button* found = nullptr;
        walk(*root, [&](ui::Widget& w) {
            auto* b = dynamic_cast<ui::Button*>(&w);
            if (b && shown(*b) && b->enabled() && (prefix ? b->text().rfind(want, 0) == 0 : b->text() == want)) found = b;
        });
        if (!found) return miss("bouton introuvable : " + name);
        return found->bounds();
    }
    if (kind == "barre") {
        // Tranche 24 (decision 53, R111-12) : une partie de la barre du haut, par le nom de la
        // commande barre de ScriptRunner (aide, nouveau, simuler...) ; barre:? est le "?" (aide), qui
        // ouvre le menu Aide (help::screen::topBarPart). Elle est dans l'ecran d'analyse : sous le
        // centre d'aide, introuvable.
        auto* root = top();
        TopBar* bar = nullptr;
        if (root) walk(*root, [&](ui::Widget& w) { if (!bar && shown(w)) bar = dynamic_cast<TopBar*>(&w); });
        if (!bar) return miss("barre du haut introuvable");
        const auto part = help::screen::topBarPart(name);
        const auto r = bar->partRect(part);
        if (r.w <= 0.f || r.h <= 0.f) return miss("partie de la barre introuvable : " + name);
        return r;
    }
    if (kind == "onglet") {
        // Un onglet de l'espace de travail, par le debut de son titre (comme la commande onglet).
        auto* root = top();
        auto* tabs = root ? dynamic_cast<ui::TabArea*>(root->findById("analysis.centre")) : nullptr;
        if (!tabs) return miss("pas d'onglets ici");
        for (std::size_t i = 0; i < tabs->tabCount(); ++i) {
            gfx::Rect r{};
            if (tabs->tab(i)->title.rfind(name, 0) != 0 || !tabs->headerRect(i, r)) continue;
            const auto* band = tabs->stripOf(i);
            const auto b = band ? band->bounds() : tabs->bounds();
            if (r.x < b.x || r.x + 24.f > b.right()) return miss("en-t\xC3\xAAte d'onglet hors de vue : " + name);
            return r;
        }
        return miss("onglet introuvable : " + name);
    }
    if (kind == "arbre") {
        // "IHM/Vues/Vue_Tuto" : chaque morceau cherche APRES le precedent, dans
        // l'ordre des lignes visibles (comme la commande arbre).
        auto* root = top();
        auto* tree = root ? dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer")) : nullptr;
        if (!tree || !tree->model()) return miss("explorateur introuvable");
        std::vector<std::string> parts;
        for (std::size_t from = 0;;) {
            const auto slash = name.find('/', from);
            parts.push_back(lower(name.substr(from, slash == std::string::npos ? std::string::npos : slash - from)));
            if (slash == std::string::npos) break;
            from = slash + 1;
        }
        std::size_t part = 0;
        for (const auto n : tree->visibleNodes()) {
            using TK = ProjectTreeModel::NodeKind;
            const auto k = ProjectTreeModel::kindOf(n);
            if (k == TK::PinsFolder || k == TK::PinItem || k == TK::RecentFolder || k == TK::RecentItem) continue;
            // 1.11.2 (T1, R1112-5 : api-compiler encadrait IHM sans jamais montrer Compiler) : la rangee d'outils sous IHM
            // (Echanges, Rechercher, Outil Modbus, Generer, Compiler) et sous API (Statistiques) a des boutons, pas de
            // libelle. Le dernier morceau du chemin peut nommer l'un d'eux (arbre:IHM/Compiler) : son rectangle a l'ecran,
            // au dernier dessin de l'arbre (TreeView::chipRect).
            if (k == TK::ToolRow) {
                if (part == 0 || part + 1 != parts.size()) continue;
                const auto chips = tree->model()->style(n).chips;
                for (std::size_t c = 0; c < chips.size(); ++c) {
                    if (lower(chips[c].label).rfind(parts[part], 0) != 0) continue;
                    gfx::Rect r{};
                    if (tree->chipRect(n, c, r)) return r;
                    return miss("bouton de l'arbre pas encore dessin\xC3\xA9 : " + name);
                }
                continue;
            }
            if (lower(tree->model()->text(n)).rfind(parts[part], 0) != 0) continue;
            if (++part < parts.size()) continue;
            gfx::Rect r{};
            if (tree->rowRect(n, r)) return r;
            if (!quiet_) tree->ensureVisible(n);
            return miss("ligne de l'arbre hors de vue : " + name);
        }
        return miss("ligne de l'arbre introuvable : " + name);
    }
    if (kind == "champ") {
        // Tranche 6 : un champ d'un dialogue (Nouveau script ST : Nom, Periode...), comme la
        // commande champ de ScriptRunner : le N-ieme champ visible du dessus, ou cet identifiant.
        auto* root = top();
        if (!root) return miss("aucun \xC3\xA9" "cran");
        std::vector<ui::InputText*> fields;
        walk(*root, [&](ui::Widget& w) {
            if (auto* t = dynamic_cast<ui::InputText*>(&w); t && shown(*t)) fields.push_back(t);
        });
        ui::InputText* found = nullptr;
        const bool number = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (number) {
            const auto n = static_cast<std::size_t>(std::atoi(name.c_str()));
            if (n >= 1 && n <= fields.size()) found = fields[n - 1];
        } else {
            for (auto* t : fields) if (t->id() == name) found = t;
        }
        if (!found) return miss("champ introuvable : " + name);
        return found->bounds();
    }
    if (kind == "ligne") {
        // Tranche 6 : la ligne d'un tableau visible dont la premiere colonne dit `name`
        // (la liste des scripts : Rampe_Vanne apres sa creation).
        auto* root = top();
        if (!root) return miss("aucun \xC3\xA9" "cran");
        std::optional<gfx::Rect> r;
        bool hidden = false;
        walk(*root, [&](ui::Widget& w) {
            auto* t = dynamic_cast<ui::TableView*>(&w);
            if (r || !t || !shown(*t) || !t->model()) return;
            for (std::size_t i = 0; i < t->visibleRowCount() && !r; ++i) {
                if (t->model()->cellText(t->viewRow(i), 0) != name) continue;
                gfx::Rect rr{};
                if (t->rowRect(i, rr)) r = rr;
                else hidden = true;
            }
        });
        if (r) return r;
        return miss(hidden ? "ligne hors de vue : " + name : "ligne introuvable : " + name);
    }
    auto* page = currentPage();
    if (!page) return miss("aucun onglet");
    if (kind == "sous-onglet") {
        // Un onglet dans l'onglet courant (l'inspecteur Proprietes / Actions...).
        std::optional<gfx::Rect> r;
        walk(*page, [&](ui::Widget& w) {
            auto* tabs = dynamic_cast<ui::TabControl*>(&w);
            if (r || !tabs || !shown(*tabs)) return;
            for (std::size_t i = 0; i < tabs->tabCount() && !r; ++i) {
                gfx::Rect rr{};
                if (tabs->tab(i)->title.rfind(name, 0) == 0 && tabs->headerRect(i, rr)) r = rr;
            }
        });
        return r ? r : miss("sous-onglet introuvable : " + name);
    }
    if (kind == "menu") {
        // Tranche 5 : une entree d'un menu ouvert (PopupMenu) ou d'une liste deroulante ouverte (DropDown).
        std::optional<gfx::Rect> r;
        if (auto* root = top())
            walk(*root, [&](ui::Widget& w) {
                if (r) return;
                if (auto* m = dynamic_cast<ui::PopupMenu*>(&w); m && m->isOpen()) {
                    for (std::size_t i = 0; i < m->items().size() && !r; ++i)
                        if (!m->items()[i].separator && m->items()[i].label == name) r = m->itemRect(i);
                } else if (auto* d = dynamic_cast<ui::DropDown*>(&w); d && d->isOpen()) {
                    const auto& items = d->items();
                    const auto box = d->popupRect();
                    const std::size_t rows = std::min<std::size_t>(items.size(), 12);
                    for (std::size_t i = 0; i < items.size() && i < rows && !r; ++i)
                        if (items[i].label == name && rows > 0) {
                            const float h = box.h / static_cast<float>(rows);
                            r = gfx::Rect{box.x, box.y + static_cast<float>(i) * h, box.w, h};
                        }
                }
            });
        return r ? r : miss("entr\xC3\xA9" "e de menu introuvable : " + name);
    }
    if (kind == "editeur") {
        // Tranche 5 : editeur:<ligne>:<colonne> dans l'editeur de code de l'onglet courant (1 = la
        // premiere). La hauteur vient de la marge des points d'arret ; la colonne est estimee
        // (8 px par caractere a 100 %) : de quoi viser, pas un caret exact.
        const auto c2 = name.find(':');
        const int line = std::atoi(name.substr(0, c2).c_str());
        const int col = c2 == std::string::npos ? 1 : std::atoi(name.substr(c2 + 1).c_str());
        ui::MultiLineText* ed = nullptr;
        walk(*page, [&](ui::Widget& w) {
            if (auto* t = dynamic_cast<ui::MultiLineText*>(&w); !ed && t && shown(*t) && !t->readOnly()) ed = t;
        });
        if (!ed) return miss("aucun \xC3\xA9" "diteur de code ici");
        if (line < 1) return miss("editeur:<ligne>:<colonne>");
        // Tranche 9 (decision du chef) : le rectangle du caractere vient de l'editeur
        // (MultiLineText::positionRect : le dernier dessin, la largeur mesuree), plus
        // d'estimation (16 px par ligne, 8 px par caractere). La cible est sa moitie
        // gauche : un clic en son milieu pose le curseur AVANT le caractere (caretAt
        // arrondit a la frontiere la plus proche). La colonne compte en octets (ST : ASCII).
        gfx::Rect at{};
        if (!ed->positionRect(static_cast<std::size_t>(line - 1), static_cast<std::uint32_t>(std::max(0, col - 1)), at))
            return miss("ligne pas en vue : " + name);
        return gfx::Rect{at.x, at.y, std::max(2.f, at.w * 0.5f), at.h};
    }
    if (kind == "saisie") {
        // Tranche 5 : la ligne de l'aide a la saisie (la liste des membres apres "V[0].")
        // dont le texte est `name`. La liste est dessinee par InputText::onPaintOverlay ;
        // sa boite se deduit de eventBounds() (le champ et la liste reunis). Liste fermee
        // ou pas encore dessinee : introuvable (la scene reessaie aux images suivantes).
        // Le champ dont la liste est ouverte : un InputText, ou la case en cours d'une grille.
        ui::InputText* field = nullptr;
        ui::MultiLineText* code = nullptr;
        if (auto* root = top())
            walk(*root, [&](ui::Widget& w) {
                if (field || !shown(w)) return;
                if (auto* f = dynamic_cast<ui::InputText*>(&w); f && f->suggestionsOpen()) field = f;
                else if (auto* g = dynamic_cast<ui::PropertyGrid*>(&w); g && g->activeField() && g->activeField()->suggestionsOpen())
                    field = g->activeField();
                else if (auto* m = dynamic_cast<ui::MultiLineText*>(&w); !code && m && m->completionOpen()) code = m;
            });
        // Tranche 10 : l'editeur de code (le script) a sa propre liste ; la ligne qui montre
        // `name` vient de MultiLineText::completionRect (d'apres le dernier dessin).
        if (!field && code) {
            gfx::Rect row{};
            if (code->completionRect(name, row)) return row;
            return miss("aide \xC3\xA0 la saisie sans " + name + " (ou pas encore dessin\xC3\xA9" "e)");
        }
        if (!field) return miss("aide \xC3\xA0 la saisie ferm\xC3\xA9" "e (" + name + ")");
        const auto& items = field->suggestions();
        std::size_t index = items.size();
        for (std::size_t i = 0; i < items.size() && index == items.size(); ++i)
            if (lower(items[i].text) == lower(name)) index = i;
        for (std::size_t i = 0; i < items.size() && index == items.size(); ++i)
            if (lower(items[i].text).rfind(lower(name), 0) == 0) index = i;
        if (index == items.size()) return miss("aide \xC3\xA0 la saisie sans " + name);
        const auto b = field->bounds(), eb = field->eventBounds();
        const bool below = eb.bottom() > b.bottom() + 0.5f;
        const bool above = eb.y < b.y - 0.5f;
        if (below == above || (eb.x <= 0.f && eb.y <= 0.f))
            return miss("aide \xC3\xA0 la saisie pas encore dessin\xC3\xA9" "e (" + name + ")");
        const float boxY = below ? b.bottom() + 1.f : eb.y;
        const float boxH = below ? eb.bottom() - boxY : b.y - 1.f - eb.y;
        constexpr std::size_t kRows = 10;   // InputText::onPaintOverlay
        const std::size_t shownRows = std::min(kRows, items.size());
        const float rowH = (boxH - 4.f) / static_cast<float>(shownRows);
        const std::size_t sel = field->suggestionIndex();
        const std::size_t first = sel >= shownRows ? sel + 1 - shownRows : 0;
        if (index < first || index >= first + shownRows) return miss("ligne de l'aide hors de vue : " + name);
        return gfx::Rect{eb.x + 2.f, boxY + 2.f + static_cast<float>(index - first) * rowH, eb.w - 4.f, rowH};
    }
    if (kind == "outil") {
        std::optional<gfx::Rect> r;
        walk(*page, [&](ui::Widget& w) {
            auto* strip = dynamic_cast<HmiToolStrip*>(&w);
            if (r || !strip || !shown(*strip)) return;
            const int a = strip->actionByTip(name);
            if (a >= 0) {
                const auto rr = strip->rectOf(a);
                if (rr.w > 0.f) r = rr;
            }
        });
        return r ? r : miss("outil introuvable : " + name);
    }
    if (kind == "macro") {
        // 1.11.2 (T1, R1112-3 de R111 : l'etape « Son formulaire » d'une macro decrivait ses champs sur la vue d'ensemble
        // de l'automate ; aucune cible ne visait une macro de la liste, sonde de la tranche 42) : la ligne d'une macro dans
        // la liste de l'onglet Macros (MacrosPane), par son nom ("ImportES") ou son chemin ("Importer depuis un CSV/ImportES").
        // Hors de vue : la liste defile vers elle (sauf pour l'encadre, qui ne change rien), l'image suivante la trouve.
        std::optional<gfx::Rect> r;
        bool known = false;
        walk(*page, [&](ui::Widget& w) {
            auto* pane = dynamic_cast<MacrosPane*>(&w);
            if (r || !pane || !shown(*pane)) return;
            const auto n = pane->nodeOf(name);
            if (n == ui::kInvalidNode) return;
            known = true;
            gfx::Rect rr{};
            if (pane->tree().rowRect(n, rr)) r = rr;
            else if (!quiet_) pane->tree().ensureVisible(n);
        });
        if (r) return r;
        return miss(known ? "macro hors de vue : " + name : "macro introuvable : " + name);
    }
    if (kind == "propriete") {
        // Tranche 8 : une case pilotee par une expression porte "  f" (f crochet) au bout de
        // son nom (HmiPanels.cpp ; la grille l'efface au dessin) : on la cherche aussi ainsi.
        const std::string driven = name + "  \xC6\x92";
        std::optional<gfx::Rect> r;
        walk(*page, [&](ui::Widget& w) {
            auto* grid = dynamic_cast<ui::PropertyGrid*>(&w);
            if (r || !grid || !shown(*grid)) return;
            gfx::Rect rr{};
            if (grid->valueRect(name, rr) || grid->valueRect(driven, rr)) r = rr;
            // hors de vue : defiler, l'image suivante la trouve
            else if (!quiet_ && !grid->revealValue(name)) (void)grid->revealValue(driven);
        });
        return r ? r : miss("propri\xC3\xA9t\xC3\xA9 introuvable : " + name);
    }
    auto* editor = currentEditor();
    // Tranche 8 : la VUE EN MARCHE. Apres F8, la page est le volet de simulation
    // (HmiSimulationPane), plus l'editeur : un objet s'y trouve par son nom (son
    // cadre dans la couche du dessus qui le montre), un point par la vue affichee.
    if (kind == "vue" && !editor) {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) pane = app_.liveHmiPane();
        if (pane && pane->visible()) {
            if (!parsePair(name, x, y)) {
                gfx::Rect r{};
                if (pane->canvas().objectRect(name, r)) return gfx::Rect{r.x, r.y, std::max(2.f, r.w), std::max(2.f, r.h)};
                return miss("objet introuvable dans la vue en marche : " + name);
            }
            const auto p = pane->canvas().viewport().toScreen(x, y);
            return gfx::Rect{p.x - 1.f, p.y - 1.f, 2.f, 2.f};
        }
    }
    if (!editor) return miss("pas d'\xC3\xA9" "diteur de vue");
    if (kind == "variante") {
        // Les cartes de variantes de la bibliotheque ne sont pas encore la (maquette
        // 1.11, scene 2) : la tuile du dernier genre vise par biblio: en tient lieu.
        if (lastKind_.empty()) return miss("variante sans biblio: avant : " + name);
        auto r = locate("biblio:" + lastKind_, why);
        if (r && !quiet_) lastVariant_ = name;
        return r;
    }
    if (kind == "alarme") {
        // Tranche 5 : une alarme sous l'objet choisi, dans l'explorateur d'objets (son noeud
        // Alarmes deplie) : la ligne dont le nom court est `name` (Course_Trop_Longue, Defaut).
        auto doc = app_.hmi();
        const auto* view = doc ? doc->project.view(editor->viewId()) : nullptr;
        if (!view) return miss("pas de vue ouverte");
        std::vector<hmi::Id> ids = editor->canvas().selection();
        for (const auto& o : view->objects) ids.push_back(o.id);
        for (const auto id : ids) {
            const auto* node = editor->objects().alarmNode(id);
            if (!node) continue;
            const auto lines = alarmtree::linesOf(*node);
            for (std::size_t k = 0; k < lines.size(); ++k) {
                if (!lines[k].alarm || lines[k].alarm->name != name) continue;
                const int line = !lines.empty() && lines[0].depth == 0 && !lines[0].alarm ? static_cast<int>(k) : static_cast<int>(k) + 1;
                if (!quiet_ && !editor->objects().alarmsOpen(id)) editor->objects().setAlarmsOpen(id, true);
                gfx::Rect r{};
                if (editor->objects().alarmRowRect(id, line, r)) return r;
                if (!quiet_) editor->objects().reveal(id);
                return miss("alarme hors de vue : " + name);
            }
        }
        return miss("alarme introuvable : " + name);
    }
    if (kind == "biblio") {
        for (auto k : hmi::kPlaceableKinds) {
            if (lower(hmi::kindLabel(k)) != lower(name)) continue;
            if (!quiet_) lastKind_ = name;
            gfx::Rect r{};
            if (editor->palette().tileRect(k, r)) return r;
            if (!quiet_) editor->palette().revealKind(k);
            return miss("tuile cach\xC3\xA9" "e : " + name);
        }
        return miss("genre inconnu : " + name);
    }
    if (kind == "vue" && !parsePair(name, x, y)) {
        // Un objet de la vue ouverte, par son nom : son cadre a l'ecran.
        auto doc = app_.hmi();
        const auto* view = doc ? doc->project.view(editor->viewId()) : nullptr;
        if (!view) return miss("pas de vue ouverte");
        for (const auto& o : view->objects) {
            if (o.name != name) continue;
            const double ox = o.number("x"), oy = o.number("y");
            const auto a = editor->canvas().viewport().toScreen(ox, oy);
            const auto b = editor->canvas().viewport().toScreen(ox + o.number("w"), oy + o.number("h"));
            return gfx::Rect{a.x, a.y, std::max(2.f, b.x - a.x), std::max(2.f, b.y - a.y)};
        }
        return miss("objet introuvable dans la vue : " + name);
    }
    if (kind == "vue") {
        const auto p = editor->canvas().viewport().toScreen(x, y);
        return gfx::Rect{p.x - 1.f, p.y - 1.f, 2.f, 2.f};
    }
    return miss("genre pas encore trouv\xC3\xA9 : " + std::string(kind));
}

} // namespace app
