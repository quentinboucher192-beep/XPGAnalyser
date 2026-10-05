#include "HmiLive.hpp"
#include "HmiCharts.hpp"
#include "HmiDisplay.hpp"
#include "HmiWidgets.hpp"
#include "HmiDuplicate.hpp"   // 1.11 (REP) : l'ancien $Vanne$ inconnu, la phrase de Dupliquer

#include <algorithm>
#include <map>

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
    bound_.clear();
    for (const auto& o : view.objects) {
        for (const auto& p : o.props) {
            if (!p.expr.empty()) {
                Bound b;
                b.object = o.id;
                b.key = p.key;
                b.expr = Expression::compile(p.expr);
                bound_.push_back(std::move(b));
            } else if (p.key == "text" && p.value.find('{') != std::string::npos) {
                // Un texte a trous se remplit en marche, sans '=' : c'est ce
                // qu'on attend d'un libelle "Pression : {PT1:0.0} bar".
                auto tpl = TextTemplate::compile(p.value);
                // Sans trou mais avec {{ ou }} (des accolades ecrites) : rendu
                // quand meme, pour que l'accolade s'affiche seule.
                const bool escaped = p.value.find("{{") != std::string::npos || p.value.find("}}") != std::string::npos;
                if (!tpl.dynamic() && !escaped) continue;
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
}

View LiveView::evaluate(sim::Environment& plc, std::vector<LiveValue>* values, const Scope* scope, double seconds) const {
    View out = source_;
    if (values) values->clear();
    // Les tableaux : leurs cases evaluees, reecrites d'un bloc a la fin.
    std::map<Id, std::vector<std::vector<std::string>>> tables;
    // Les images animees : le premier etat vrai (le rang), par objet.
    std::map<Id, int> chosen;
    // Lot 11 : les listes, par (objet, cle) : une valeur par element.
    std::map<std::pair<Id, std::string>, std::vector<std::string>> lists;
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
            else if (auto v = b.expr.evaluate(plc, scope))
                // Le tableau de variables montre ses reels a son format (0.00 : 60.00).
                // Lot 13 : le format de la ligne ("formats", sinon celui de l'objet).
                lv.value = o->kind == Kind::VariableTable && v->type() == sim::Type::Real
                               ? formatValue(*v, rowFormat(nullptr, *o, static_cast<std::size_t>(b.item)))
                               : formatValue(*v);
            else { lv.error = true; lv.value = liveError(lv.expression, v.error().message()); }
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
                lv.value = b.text.render(plc, scope);
                lv.error = !b.text.errors().empty() || lv.value.find("###") != std::string::npos;
            } else {
                lv.expression = "=" + b.expr.source();
                if (!b.expr.valid()) { lv.error = true; lv.value = b.expr.error(); }
                else if (auto v = b.expr.evaluate(plc, scope)) lv.value = formatValue(*v);
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
                if (v->isTruthy() && !chosen.count(b.object)) chosen[b.object] = b.state;
            } else { lv.error = true; lv.value = liveError(lv.expression, v.error().message()); }
            if (values) values->push_back(std::move(lv));
            continue;
        }
        if (b.isText) {
            lv.expression = b.text.source();
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
                lv.value = formatValue(*v);
                // Une rotation, une position, une couleur : la valeur REMPLACE
                // la statique. L'expression reste (l'objet est toujours pilote).
                if (b.key == "rot") {
                    double a = 0;
                    if (parseNumber(lv.value, a)) o->setNumber("rot", a);
                } else {
                    o->set(b.key, lv.value);
                }
            } else {
                lv.error = true;
                lv.value = liveError(lv.expression, v.error().message());
            }
            // Lot 9 : une valeur illisible, l'afficheur le montre (qualite mauvaise).
            if (lv.error && b.key == "value") o->setFlag("quality_bad", true);
        }
        if (values) values->push_back(std::move(lv));
    }
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
