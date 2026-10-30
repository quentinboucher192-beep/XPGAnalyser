#include "HmiLive.hpp"
#include "HmiCharts.hpp"
#include "HmiDisplay.hpp"
#include "HmiWidgets.hpp"
#include "HmiDuplicate.hpp"   // 1.11 (REP) : l'ancien $Vanne$ inconnu, la phrase de Dupliquer
#include "HmiLiveGeometry.hpp"  // 1.11.4 : une instance, un groupe dont la geometrie vient d'une formule
#include "HmiNatives.hpp"        // 1.12.1 : les enumerations natives des proprietes
#include "HmiMarkers.hpp"        // 1.12.5 : un texte montre sans ses reperes
#include "HmiObjectAlarms.hpp"   // 1.12.5 (D13) : une liaison tapee sans =
#include "HmiNavigation.hpp"      // 1.12.5 : la page d'un conteneur a onglets selon sa variable
#include "HmiEquipmentLink.hpp"     // 1.12.11 (lot G) : le mode Etat d'un symbole du synoptique

#include <algorithm>
#include <map>
#include <set>

namespace hmi {
namespace {

// 1.10.3 : le message d'un calcul qui echoue, en francais. Une erreur sans detail
// disait « invalid argument » (vu dans l'onglet Expressions de la simulation) :
// « argument invalide » ; « invalid argument: detail » : le detail seul, comme le
// dialecte IHM de l'interpreteur (sim/Interpreter.cpp, fail).
std::string liveErrorText(std::string m) {
    constexpr std::string_view kPrefix = "invalid argument: ";
    if (m == "invalid argument") return "argument invalide";
    if (m.rfind(kPrefix, 0) == 0) m.erase(0, kPrefix.size());
    // "'ConfigsGaz[15].Utilise' is not declared" (Voyant_Config de Vue_Armoire_A) :
    // comme l'onglet de l'automate (SimulationPane) : "... n'est pas declaree".
    constexpr std::string_view kUndeclared = "' is not declared";
    if (m.size() > kUndeclared.size() + 1 && m.front() == '\'' &&
        m.compare(m.size() - kUndeclared.size(), kUndeclared.size(), kUndeclared) == 0)
        return m.substr(1, m.size() - kUndeclared.size() - 1) + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e";
    if (m == "division by zero" || m == "modulo by zero") return "division par z\xC3\xA9ro";
    return m;
}
} // namespace

// 1.11 (REP) : une expression a repere se calcule comme les autres (ses $ sont
// transparents ; 1.10.3 : "repere non remplace", plus calculee). L'ancien $Vanne$
// dont le nom n'est pas une variable : l'erreur du calcul, et la phrase de Dupliquer.
namespace {
std::string liveError(std::string_view source, const std::string& message) {
    std::string m = liveErrorText(message);
    if (source.find('$') == std::string_view::npos) return m;
    constexpr std::string_view kUndeclared = " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e";
    if (m.size() > kUndeclared.size() && m.compare(m.size() - kUndeclared.size(), kUndeclared.size(), kUndeclared) == 0) {
        const auto roots = scanRoots(std::string_view(m).substr(0, m.size() - kUndeclared.size()));
        if (!roots.empty()) m += dup::markerHint(source, roots.front());
    }
    return m;
}
} // namespace

std::string autoValueSource(const Object& o) {
    const auto* v = o.find("value");
    if (v && !v->expr.empty()) return {};
    if (o.kind == Kind::InputField) {
        // (Pas en mode mot de passe : il ne s'affiche pas, meme dans le tableau des expressions.)
        if (o.text("variable").empty() || o.text("mode").rfind("mot", 0) == 0) return {};
        return o.text("variable");
    }
    if (!kindWritesVariable(o.kind) && !kindShowsValue(o.kind)) return {};
    if (o.kind == Kind::WeeklySchedule || o.kind == Kind::DateTimePicker) return {};
    std::string source;
    if (kindWritesVariable(o.kind)) {
        const std::string lamp = o.kind == Kind::IlluminatedButton ? o.text("lamp") : std::string{};
        const std::string state = o.text("state");
        source = !lamp.empty() ? lamp : !state.empty() ? state : o.text("variable");
    } else {
        source = o.text("variable");
    }
    return source.find_first_not_of(" \t") == std::string::npos ? std::string{} : source;
}

void LiveView::bind(const View& view) {
    source_ = view;
    shown_ = view;
    for (auto& o : shown_.objects) roles::plainProps(o);   // 1.12.6 (lot C) : les reperes, retires selon le role
    // 1.14.1 (captures finales) : LE TEXTE TAPE SE LIT ICI, UNE FOIS - ses \n (un retour a la ligne) et \t (une tabulation),
    // dans un texte montre (libelle, titre, invite...) et une liste de textes (les colonnes d'un tableau, les noms des
    // plumes...). Le dessin en marche montre ensuite la vue telle quelle : une valeur ({Fichier}, =Chemin, la variable d'un
    // afficheur) garde ses \ (C:\travaux\nouveau, Z:\tmp\...). Un texte a trous lit les siens a sa compilation.
    for (auto& o : shown_.objects)
        for (auto& p : o.props) {
            if (!p.expr.empty() || p.value.find('\\') == std::string::npos) continue;
            const roles::KeyRole r = roles::keyRole(p.key) ? roles::roleOf(o.kind, p.key) : roles::KeyRole{};
            const bool textList = r.role == roles::Role::List && r.item == roles::Role::Text;
            if (displayedTextKey(p.key) || textList) p.value = unescapeText(p.value);
            // ... les cases fixes d'un Tableau (une case =expression ou a trous se calcule plus bas) : un \n y devient un
            // blanc (une case tient sur une ligne).
            if (o.kind == Kind::Table && p.key == "cells") {
                auto rows = parseCells(p.value);
                for (auto& row : rows)
                    for (auto& cell : row)
                        if (!cellIsExpression(cell) && !cellIsTemplate(cell)) cell = unescapeText(cell);
                p.value = formatCells(rows);
            }
        }
    bound_.clear();
    for (const auto& o : view.objects) {
        for (const auto& p : o.props) {
            // 1.12.11 (lot G) : la liaison Equipement (un chemin de structure) et le mode Etat ne se calculent
            // pas : le moteur a deja rempli les roles de l'objet (eqlink::apply, la vue composee).
            if (p.key == eqlink::kEquipmentKey || p.key == eqlink::kAspectsKey) continue;
            if (!p.expr.empty()) {
                Bound b;
                b.object = o.id;
                b.key = p.key;
                b.expr = Expression::compile(p.expr);
                bound_.push_back(std::move(b));
            } else if (synopticLinkedKey(o, p.key) || formulaValueKey(o, p.key)) {
                // 1.12.5 (lot A, D13) : le Defaut, le Mouvement ou la Valeur d'un symbole du
                // synoptique tape sans '=' (P1.Defaut) : une formule, comme pour son alarme.
                // 1.12.31 : TOUTE PROPRIETE DE ROLE FORMULE dont la valeur se montre, tapee dans
                // l'inspecteur (sans son =) - les fins de course et le declenchement des registres,
                // les booleens A et B d'une vanne 3 voies, une condition... : calculee comme une
                // expression, selon la table des roles (avant : leur texte, lu comme une constante).
                Bound b;
                b.object = o.id;
                b.key = p.key;
                b.expr = Expression::compile(p.value);
                bound_.push_back(std::move(b));
            } else if (displayedTextKey(p.key) && (p.value.find('{') != std::string::npos || p.value.find('$') != std::string::npos)) {
                // Un texte a trous se remplit en marche, sans '=' : c'est ce
                // qu'on attend d'un libelle "Pression : {PT1:0.0} bar".
                // 1.12.5 (lot A, D6) : chaque texte montre (libelle, titre, texte des etats,
                // invite...), plus seulement « Texte » ; et un texte sans trou mais avec un
                // repere ("Mode : $Maintenance$", ce que Dupliquer ecrit) se montre sans ses $.
                auto tpl = TextTemplate::compile(p.value);
                // Sans trou mais avec {{ ou }} (des accolades ecrites) : rendu
                // quand meme, pour que l'accolade s'affiche seule.
                const bool escaped = p.value.find("{{") != std::string::npos || p.value.find("}}") != std::string::npos;
                const bool marked = markers::strip(p.value, markers::Mode::Text) != p.value;
                if (!tpl.dynamic() && !escaped && !marked) continue;
                Bound b;
                b.object = o.id;
                b.key = p.key;
                b.isText = true;
                b.text = std::move(tpl);
                bound_.push_back(std::move(b));
            }
        }
        // Lot 6 : les cases d'un tableau ("=expression" ou texte a trous) et les
        // conditions des etats d'une image animee.
        if (o.kind == Kind::Table)
            if (const auto* cells = o.find("cells"); cells && !cells->value.empty()) {
                const auto rows = parseCells(cells->value);
                for (std::size_t r = 0; r < rows.size(); ++r)
                    for (std::size_t c = 0; c < rows[r].size(); ++c) {
                        const auto& cell = rows[r][c];
                        Bound b;
                        b.object = o.id;
                        b.key = "cells";
                        b.row = static_cast<int>(r);
                        b.col = static_cast<int>(c);
                        if (cellIsExpression(cell)) {
                            b.expr = Expression::compile(cell.substr(1));
                        } else if (cellIsTemplate(cell)) {
                            auto tpl = TextTemplate::compile(cell);
                            if (!tpl.dynamic() && cell.find("{{") == std::string::npos && cell.find("}}") == std::string::npos)
                                continue;
                            b.isText = true;
                            b.text = std::move(tpl);
                        } else {
                            continue;
                        }
                        bound_.push_back(std::move(b));
                    }
            }
        // Lot 8 : le champ de saisie montre la valeur de sa variable. Lot 9 : une
        // commande montre son retour d'etat (sinon sa variable ; le bouton
        // lumineux, son voyant) ; un afficheur, sa variable quand sa valeur n'a
        // pas d'expression (autoValueSource). L'interrupteur relit aussi sa
        // commande : si elle differe du retour d'etat, il le montre (discordance).
        if (const std::string source = autoValueSource(o); !source.empty()) {
            Bound b;
            b.object = o.id;
            b.key = "value";
            b.expr = Expression::compile(source);
            bound_.push_back(std::move(b));
        }
        if (kindWritesVariable(o.kind) || kindShowsValue(o.kind)) {
            if (o.kind == Kind::Switch && !o.text("state").empty() && !o.text("variable").empty()) {
                Bound b;
                b.object = o.id;
                b.key = "command";
                b.expr = Expression::compile(o.text("variable"));
                bound_.push_back(std::move(b));
            }
        }
        // Lot 12 : un conteneur a onglets (ou un panneau repliable) relie a une
        // variable montre la page qu'elle dit (ou se replie quand elle est vraie).
        if ((o.kind == Kind::TabContainer || o.kind == Kind::CollapsiblePanel)
            && o.text("variable").find_first_not_of(" \t") != std::string::npos) {
            const char* key = o.kind == Kind::TabContainer ? "page" : "collapsed";
            if (const auto* p = o.find(key); !p || p->expr.empty()) {
                Bound b;
                b.object = o.id;
                b.key = key;
                b.expr = Expression::compile(o.text("variable"));
                bound_.push_back(std::move(b));
            }
        }
        // Lot 11 : les listes d'expressions des graphiques evalues a chaque image
        // (barres, camembert, radar et sa consigne) et du tableau de variables.
        if (o.kind == Kind::BarChart || o.kind == Kind::PieChart || o.kind == Kind::RadarChart || o.kind == Kind::VariableTable) {
            for (const char* listKey : {"variables", "references"}) {
                if (std::string_view(listKey) == "references" && o.kind != Kind::RadarChart) continue;
                const auto items = chartItems(o, listKey);
                for (std::size_t k = 0; k < items.size(); ++k) {
                    Bound b;
                    b.object = o.id;
                    b.key = listKey;
                    b.item = static_cast<int>(k);
                    b.expr = Expression::compile(items[k].expression);
                    bound_.push_back(std::move(b));
                }
            }
        }
        if (o.kind == Kind::AnimatedImage)
            if (const auto* st = o.find("states"); st && !st->value.empty()) {
                const auto states = parseImageStates(st->value);
                for (std::size_t k = 0; k < states.size(); ++k) {
                    Bound b;
                    b.object = o.id;
                    b.key = "states";
                    b.state = static_cast<int>(k);
                    b.expr = Expression::compile(states[k].condition.empty() ? std::string("TRUE") : states[k].condition);
                    bound_.push_back(std::move(b));
                }
            }
    }
    // 1.12.6 (lot B) : le role de chaque propriete liee - son lecteur (HmiRoles.hpp). Un element
    // d'une liste (une barre, une part, une ligne) : le role de ses valeurs ; la commande relue d'un
    // interrupteur : un booleen.
    for (auto& b : bound_) {
        const Object* o = source_.object(b.object);
        if (!o || b.isText || b.row >= 0 || b.state >= 0) continue;
        if (b.item >= 0) {
            b.role = roles::KeyRole{b.key, roles::valueRoleOf(o->kind, b.key)};
            b.typed = true;
        } else if (b.key == "command") {
            b.role = roles::KeyRole{b.key, roles::Role::Boolean};
            b.typed = true;
        } else if (roles::keyRole(b.key)) {
            b.role = roles::roleOf(o->kind, b.key);
            b.typed = true;
        }
        // 1.12.11 (lot G) : un symbole en mode Etat lit sa Valeur comme un etat (son nombre, le nom d'une valeur
        // d'enumeration) - avant, vrai / faux : un moteur en DEFAUT (2) tournait en vert.
        if (b.key == "value" && b.typed && eqlink::hasAspects(*o)) b.role.role = roles::Role::State;
        // ... un verin « rentre / sorti » la lit vrai / faux (un BOOL vrai : sorti ; avant, 1 % de sa course).
        if (b.key == "value" && b.typed && eqlink::cylinderOnOff(*o)) b.role = roles::KeyRole{b.key, roles::Role::Boolean};
    }
}

View LiveView::evaluate(sim::Environment& plc, std::vector<LiveValue>* values, const Scope* scope, double seconds) const {
    View out = shown_;
    if (values) values->clear();
    // Les tableaux : leurs cases evaluees, reecrites d'un bloc a la fin.
    std::map<Id, std::vector<std::vector<std::string>>> tables;
    // Les images animees : le premier etat vrai (le rang), par objet.
    std::map<Id, int> chosen;
    // Lot 11 : les listes, par (objet, cle) : une valeur par element.
    std::map<std::pair<Id, std::string>, std::vector<std::string>> lists;
    // 1.11.4 : les (objet, cle) dont la formule a donne une position, une taille, une rotation.
    std::set<std::pair<Id, std::string>> geometry;
    // 1.12.6 (lot B) : la valeur d'une enumeration du projet (Mode vaut 1) : son texte (« Auto ») ;
    // le type du chemin est demande une fois par liaison.
    const auto enumItem = [&](const Bound& b, const sim::Value& v) -> const sim::Environment::EnumItem* {
        if (!sim::isInteger(v.type()) || v.type() == sim::Type::Time) return nullptr;
        if (b.enumAsked < 0) {
            const std::string type = b.expr.pathType(plc, scope);
            b.enumAsked = !type.empty() && plc.enumItems(type, b.enumItems) ? 1 : 0;
        }
        if (b.enumAsked != 1) return nullptr;
        for (const auto& it : b.enumItems)
            if (it.value == v.asInteger()) return &it;
        return nullptr;
    };
    // La valeur lue dans le role de la liaison ; faux (et `why`) : elle ne convient pas.
    const auto readAs = [&](const Bound& b, const sim::Value& v, std::string& text, std::string& why, std::string* name) {
        roles::Shape shape;
        const auto role = b.role.role == roles::Role::Formula ? b.role.item : b.role.role;
        if (role == roles::Role::Text || role == roles::Role::State)
            if (const auto* it = enumItem(b, v)) {
                shape.enumText = it->text;
                shape.enumName = it->name;
            }
        auto read = roles::adapt(b.role, v, shape);
        if (!read.ok) { why = std::move(read.why); return false; }
        text = std::move(read.text);
        if (name) *name = std::move(read.name);
        return true;
    };
    for (const auto& b : bound_) {
        Object* o = out.object(b.object);
        if (!o) continue;
        LiveValue lv;
        lv.object = b.object;
        lv.objectName = o->name;
        lv.key = b.key;
        if (b.item >= 0) {
            lv.key = b.key + "[" + std::to_string(b.item + 1) + "]";
            lv.expression = b.expr.source();
            if (!b.expr.valid()) { lv.error = true; lv.value = b.expr.error(); }
            else if (auto v = b.expr.evaluate(plc, scope)) {
                // Le tableau de variables montre ses reels a son format (0.00 : 60.00).
                // Lot 13 : le format de la ligne ("formats", sinon celui de l'objet).
                // 1.12.5 (lot A, D4) : les autres sans perte (une barre, une part : 1234567.8).
                // 1.12.6 (lot B) : lus dans le role de leurs valeurs - une barre d'un BOOL vaut 1, d'un
                // TIME des secondes ; une ligne d'un tableau, le texte d'une enumeration ; un texte
                // 'abc' dans une barre : ### (avant : 0, sans rien dire).
                std::string why;
                if (o->kind == Kind::VariableTable && v->type() == sim::Type::Real)
                    lv.value = formatValue(*v, rowFormat(nullptr, *o, static_cast<std::size_t>(b.item)));
                else if (!readAs(b, *v, lv.value, why, nullptr)) { lv.error = true; lv.value = why; }
            } else { lv.error = true; lv.value = liveError(lv.expression, v.error().message()); }
            auto& list = lists[{b.object, b.key}];
            if (list.size() <= static_cast<std::size_t>(b.item)) list.resize(static_cast<std::size_t>(b.item) + 1);
            list[static_cast<std::size_t>(b.item)] = lv.error ? std::string("###") : lv.value;
            if (values) values->push_back(std::move(lv));
            continue;
        }
        if (b.row >= 0) {
            // Une case : sa valeur remplace sa source, dans la copie du tableau.
            auto it = tables.find(b.object);
            if (it == tables.end()) {
                const auto* cells = o->find("cells");
                it = tables.emplace(b.object, parseCells(cells ? cells->value : std::string{})).first;
            }
            lv.key = "cells[" + std::to_string(b.row + 1) + "," + std::to_string(b.col + 1) + "]";
            if (b.isText) {
                lv.expression = b.text.source();
                lv.text = true;
                lv.value = b.text.render(plc, scope);
                lv.error = !b.text.errors().empty() || lv.value.find("###") != std::string::npos;
            } else {
                lv.expression = "=" + b.expr.source();
                if (!b.expr.valid()) { lv.error = true; lv.value = b.expr.error(); }
                else if (auto v = b.expr.evaluate(plc, scope)) lv.value = roles::displayText(*v);   // 1.12.6 : le role Texte
                else { lv.error = true; lv.value = liveError(b.expr.source(), v.error().message()); }
            }
            auto& rows = it->second;
            if (static_cast<std::size_t>(b.row) < rows.size() && static_cast<std::size_t>(b.col) < rows[static_cast<std::size_t>(b.row)].size())
                rows[static_cast<std::size_t>(b.row)][static_cast<std::size_t>(b.col)] = lv.error ? std::string("###") : lv.value;
            if (values) values->push_back(std::move(lv));
            continue;
        }
        if (b.state >= 0) {
            lv.key = "\xC3\xA9tat " + std::to_string(b.state + 1);
            lv.expression = b.expr.source();
            if (!b.expr.valid()) { lv.error = true; lv.value = b.expr.error(); }
            else if (auto v = b.expr.evaluate(plc, scope)) {
                lv.value = formatValue(*v);
                // 1.12.6 (lot B) : une condition lue en booleen (une STRING 'TRUE', 'ON' aussi)
                const bool on = v->type() == sim::Type::String ? roles::trueWord(v->asString()) : v->isTruthy();
                if (on && !chosen.count(b.object)) chosen[b.object] = b.state;
            } else { lv.error = true; lv.value = liveError(lv.expression, v.error().message()); }
            if (values) values->push_back(std::move(lv));
            continue;
        }
        if (b.isText) {
            lv.expression = b.text.source();
            lv.text = true;
            lv.value = b.text.render(plc, scope);
            const auto errs = b.text.errors();
            lv.error = !errs.empty() || lv.value.find("###") != std::string::npos;
            o->set(b.key, lv.value);
        } else {
            lv.expression = b.expr.source();
            if (!b.expr.valid()) {
                lv.error = true;
                lv.value = b.expr.error();
            } else if (auto v = b.expr.evaluate(plc, scope)) {
                // 1.12.6 (lot B) : LE LECTEUR DU ROLE (HmiRoles.cpp) - la valeur d'une jauge d'un BOOL vaut 1,
                // d'un TIME des secondes ; un texte montre une enumeration par son texte, un BOOL Vrai / Faux ;
                // un etat garde aussi le nom de sa valeur (le multi-etats Auto = ...). Une valeur qui ne
                // convient pas (un texte 'abc' dans une jauge) : l'erreur, et l'objet le montre (###).
                // 1.12.5 (lot A, D4) : une valeur relue en nombre passe sans perte.
                bool usable = true;
                std::string why, name;
                if (b.typed) usable = readAs(b, *v, lv.value, why, &name);
                else lv.value = displayedTextKey(b.key) ? formatValue(*v) : exactValue(*v);
                if (!usable) {
                    lv.error = true;
                    lv.value = why;
                } else {
                    if (b.key == "value" && !name.empty()) o->set("valueName", name);
                    // 1.12.1 : une enumeration native (ALIGNEMENT#Centre vaut 1, un DINT) :
                    // la propriete recoit le mot de l'inspecteur ("centre").
                    if (auto word = natives::propertyWord(kindKey(o->kind), b.key, *v)) lv.value = std::move(*word);
                    // 1.12.5 (lot A) : la page d'un conteneur a onglets selon le type de sa variable - une
                    // enumeration le rang de sa valeur (pageValues), un BOOL vrai la page 2, une STRING le nom
                    // de l'onglet ; le repli d'un panneau, allume comme un voyant (2 aussi, 'TRUE').
                    if (o->kind == Kind::TabContainer && b.key == "page") lv.value = std::to_string(tabPageFrom(*o, *v));
                    if (o->kind == Kind::CollapsiblePanel && b.key == "collapsed")
                        lv.value = (v->type() == sim::Type::String ? valueOn(v->asString()) : v->isTruthy()) ? "TRUE" : "FALSE";
                    // Une rotation, une position, une couleur : la valeur REMPLACE
                    // la statique. L'expression reste (l'objet est toujours pilote).
                    if (b.key == "rot") {
                        double a = 0;
                        if (parseNumber(lv.value, a)) o->setNumber("rot", a);
                    } else {
                        o->set(b.key, lv.value);
                    }
                    if (isLiveGeometryKey(b.key)) geometry.insert({b.object, b.key});   // 1.11.4
                }
            } else {
                // 1.12.6 (lot B) : une structure, un tableau, une liste entiers - un texte les montre
                // ({Marche: Vrai, Vitesse: 50}, 1 ; 2 ; 3) ; ailleurs, l'erreur dit quoi choisir.
                const auto shape = b.typed ? b.expr.shapeOf(plc, scope, nullptr) : roles::Shape{};
                if (!shape.aggregate.empty()) {
                    const auto read = roles::adapt(b.role, sim::Value{}, shape);
                    if (read.ok) {
                        lv.value = read.text;
                        o->set(b.key, lv.value);
                    } else {
                        lv.error = true;
                        lv.value = read.why;
                    }
                } else {
                    lv.error = true;
                    lv.value = liveError(lv.expression, v.error().message());
                }
            }
            // Lot 9 : une valeur illisible, l'afficheur le montre (qualite mauvaise).
            if (lv.error && b.key == "value") o->setFlag("quality_bad", true);
        }
        if (values) values->push_back(std::move(lv));
    }
    // 1.11.4 : une position, une taille, une rotation calculee - ce qu'une instance ou un
    // groupe contient suit (avant : seule l'instance bougeait, et elle ne se dessine pas).
    relayoutLive(out, source_, geometry);
    for (auto& [id, rows] : tables)
        if (Object* o = out.object(id)) o->set("cells", formatCells(rows));
    for (const auto& [where, list] : lists)
        if (Object* o = out.object(where.first)) o->set(where.second == "references" ? "liveReferences" : "liveValues", joinLiveValues(list));
    // L'image du moment : celle de l'etat retenu, a son rang de defilement ;
    // aucun etat vrai : l'image par defaut (la propriete "image").
    for (const auto& [id, state] : chosen)
        if (Object* o = out.object(id)) {
            const auto* st = o->find("states");
            const auto states = parseImageStates(st ? st->value : std::string{});
            if (static_cast<std::size_t>(state) < states.size()) {
                const auto frame = imageFrame(states[static_cast<std::size_t>(state)],
                                              static_cast<int>(o->number("period", 500)), seconds);
                if (!frame.empty()) o->set("image", frame);
            }
            o->setNumber("stateShown", state + 1);
        }
    return out;
}

} // namespace hmi
