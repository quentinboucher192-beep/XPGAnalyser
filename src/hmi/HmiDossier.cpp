#include "HmiDossier.hpp"
#include "HmiPopupParams.hpp"
#include "HmiTypes.hpp"
#include "HmiDisplay.hpp"
#include "HmiLanguages.hpp"

#include "HmiControls.hpp"
#include "HmiExport.hpp"
#include "HmiNavigation.hpp"
#include "HmiScenarios.hpp"
#include "HmiStore.hpp"
#include "HmiSymbols.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <utility>

namespace hmi {

namespace {

using K = DossierBlock::Kind;

// ================================================================= le contenu ===
struct Writer {
    Dossier& d;
    void add(K kind, std::string text) {
        DossierBlock b;
        b.kind = kind;
        b.text = std::move(text);
        d.blocks.push_back(std::move(b));
    }
    void table(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<double> widths = {}) {
        DossierBlock b;
        b.kind = K::Table;
        b.headers = std::move(headers);
        b.rows = std::move(rows);
        b.widths = std::move(widths);
        d.blocks.push_back(std::move(b));
    }
    void image(int index) {
        DossierBlock b;
        b.kind = K::Image;
        b.image = index;
        d.blocks.push_back(std::move(b));
    }
};

std::string yesNo(bool b) { return b ? "oui" : "non"; }

std::string joinWith(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

// Les vues ou mene une vue : ses actions, ses barres de navigation, ses zones.
std::vector<std::string> targetsOf(const Project& p, const View& v) {
    std::set<std::string> out;
    const auto action = [&](const Action& a) {
        if ((a.operation == Operation::Navigate || a.operation == Operation::Popup || a.operation == Operation::ChangePopup) && !a.target.empty())
            out.insert(a.target);
    };
    for (const auto& a : v.actions) action(a);
    for (const auto& o : v.objects) {
        for (const auto& a : o.actions) action(a);
        if (o.kind == Kind::NavBar)
            for (const auto& item : navItems(&p, o)) out.insert(item.view);
        if (o.kind == Kind::ZoneMap)
            for (const auto& z : parseMapZones(o.text("mapZones"))) if (!z.view.empty()) out.insert(z.view);
    }
    out.erase(v.name);
    return {out.begin(), out.end()};
}

// "Bouton x4, Texte x3" : les objets par genre, les plus nombreux d'abord.
std::string kindsOf(const View& v) {
    std::map<std::string, int> counts;
    for (const auto& o : v.objects) ++counts[std::string(kindLabel(o.kind))];
    std::vector<std::pair<std::string, int>> list(counts.begin(), counts.end());
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string out;
    for (std::size_t i = 0; i < list.size() && i < 8; ++i)
        out += (i ? ", " : "") + list[i].first + (list[i].second > 1 ? " \xC3\x97" + std::to_string(list[i].second) : std::string{});
    if (list.size() > 8) out += ", ...";
    return out.empty() ? std::string("aucun") : out;
}

std::string roleName(std::string_view role) {
    if (role == "vue") return "vue";
    if (role == "popup") return "popup";
    if (role == "modele") return "\xC3\xA9" "cran mod\xC3\xA8le";
    if (role == "entete") return "en-t\xC3\xAAte";
    if (role == "pied") return "pied de page";
    if (role == "symbole") return "symbole";
    return std::string(role);
}

std::string eventName(const Script& s) {
    std::string e = std::string(eventLabel(s.event));
    if (s.event == "Cyclique" || s.event == "OnCycle") e += " (" + std::to_string(s.periodMs) + " ms)";
    if (s.event == "Changement" && !s.watch.empty()) e += " : " + s.watch;
    return e;
}

} // namespace

Dossier buildDossier(const Project& p, const std::map<Id, DossierImage>& thumbnails, const DossierOptions& opt) {
    Dossier d;
    Writer w{d};
    const auto& cfg = p.config;
    d.title = "Dossier de l'IHM " + cfg.name;
    d.author = cfg.author;
    d.date = nowStamp();
    const Stats st = p.statistics();
    std::size_t popups = 0, templates = 0, symbols = 0, ordinary = 0;
    for (const auto& v : p.views) {
        if (v.role == "popup") ++popups;
        else if (v.role == "vue") ++ordinary;
        else if (isSymbolView(v)) ++symbols;
        else ++templates;
    }

    // ---- la page de garde
    w.add(K::Title, d.title);
    w.add(K::Subtitle, cfg.description.empty() ? std::string("Documentation du projet IHM") : cfg.description);
    {
        const View* start = p.view(cfg.startView);
        w.table({"", ""},
                {{"Projet", cfg.name},
                 {"Version", cfg.version},
                 {"Auteur", cfg.author.empty() ? std::string("-") : cfg.author},
                 {"Cr\xC3\xA9\xC3\xA9", cfg.created.empty() ? std::string("-") : cfg.created},
                 {"Modifi\xC3\xA9", cfg.modified.empty() ? std::string("-") : cfg.modified},
                 {"Dossier g\xC3\xA9n\xC3\xA9r\xC3\xA9 le", d.date},
                 {"R\xC3\xA9solution", std::to_string(cfg.width) + " \xC3\x97 " + std::to_string(cfg.height) + " (" + cfg.orientation + ")"},
                 {"Vue de d\xC3\xA9marrage", start ? start->name : std::string("-")}},
                {0.32, 0.68});
    }
    w.add(K::Note, "Ce dossier est produit par XpgAnalyzer \xC3\xA0 partir du projet : il d\xC3\xA9" "crit l'IHM telle qu'elle est enregistr\xC3\xA9" "e. "
                   "Les mots de passe, les codes et les badges n'y figurent jamais.");
    w.add(K::Heading2, "Sommaire");
    std::vector<std::string> sections = {"1. Pr\xC3\xA9sentation", "2. Les vues", "3. Les variables IHM", "4. Les alarmes", "5. Les recettes",
                                         "6. Les utilisateurs et la s\xC3\xA9" "curit\xC3\xA9", "7. Les scripts et les fonctions", "8. Les historiques",
                                         "9. Les essais de r\xC3\xA9" "ception", "10. Les ressources et les styles"};
    for (const auto& s : sections) w.add(K::Paragraph, s);
    w.add(K::PageBreak, {});

    // ---- 1. la presentation
    w.add(K::Heading1, sections[0]);
    w.add(K::Paragraph, cfg.description.empty() ? "Le projet IHM " + cfg.name + "." : cfg.description);
    w.table({"Ce que contient le projet", "Nombre"},
            {{"Vues ordinaires", std::to_string(ordinary)},
             {"Popups", std::to_string(popups)},
             {"\xC3\x89" "crans mod\xC3\xA8les, en-t\xC3\xAAtes et pieds de page", std::to_string(templates)},
             {"Symboles", std::to_string(symbols)},
             {"Objets", std::to_string(st.objects)},
             {"Propri\xC3\xA9t\xC3\xA9s anim\xC3\xA9" "es (expressions)", std::to_string(st.expressions)},
             {"Scripts", std::to_string(st.scripts)},
             {"Actions", std::to_string(st.actions)},
             {"Variables IHM", std::to_string(p.programs.variables.size())},
             {"Fonctions IHM", std::to_string(p.programs.functions.size())},
             {"Alarmes", std::to_string(p.alarms.size())},
             {"Recettes", std::to_string(p.recipes.size())},
             {"Comptes utilisateurs", std::to_string(p.security.users.size())},
             {"Ressources", std::to_string(p.assets.resources.size())},
             {"Essais de r\xC3\xA9" "ception", std::to_string(p.scenarios.size())}},
            {0.72, 0.28});
    w.table({"R\xC3\xA9glage", "Valeur"},
            {{"Cycle IHM", std::to_string(cfg.cycleMs) + " ms"},
             {"Changer de vue en glissant", yesNo(cfg.swipeNavigation)},
             {"Contr\xC3\xB4les de qualit\xC3\xA9 (G\xC3\xA9n\xC3\xA9rer)", yesNo(cfg.quality)},
             {"S\xC3\xA9" "curit\xC3\xA9", p.security.enabled ? std::string("active") : std::string("inactive")},
             {"Journal d'audit", yesNo(p.history.audit)},
             // lot 13 : les langues, l'affichage au lancement
             {"Langues", [&] {
                  std::string all;
                  for (const auto& l : p.languages.list) all += (all.empty() ? "" : ", ") + (l.name.empty() ? l.code : l.name) + " (" + l.code + ")";
                  return all;
              }()},
             {"Affichage au lancement", std::to_string(cfg.textScale) + " %, couleurs " + cfg.colorMode + ", symboles "
                                            + (cfg.statusSymbols ? std::string("oui") : std::string("non")) + ", th\xC3\xA8me " + cfg.theme}},
            {0.6, 0.4});
    // Lot 13 : ce qui est traduit, langue par langue.
    if (p.languages.list.size() > 1) {
        w.add(K::Heading2, "Les langues");
        std::vector<std::vector<std::string>> rows;
        for (const auto& c : coverage(p))
            rows.push_back({c.name + " (" + c.code + ")", std::to_string(c.translated) + " / " + std::to_string(c.total),
                            c.total ? std::to_string(c.translated * 100 / c.total) + " %" : std::string("-")});
        w.table({"Langue", "Textes traduits", "Part"}, std::move(rows), {0.5, 0.3, 0.2});
    }

    // ---- 2. les vues
    w.add(K::PageBreak, {});
    w.add(K::Heading1, sections[1]);
    w.add(K::Paragraph, "Chaque vue avec sa vignette (telle que l'\xC3\xA9" "diteur la dessine), sa taille, ses objets et les vues o\xC3\xB9 elle m\xC3\xA8ne.");
    const auto describe = [&](const View& v) {
        w.add(K::Heading2, v.name);
        if (const auto it = thumbnails.find(v.id); it != thumbnails.end() && !it->second.jpeg.empty()) {
            d.images.push_back(it->second);
            if (d.images.back().caption.empty()) d.images.back().caption = v.name;
            w.image(static_cast<int>(d.images.size()) - 1);
        }
        if (!v.description.empty()) w.add(K::Paragraph, v.description);
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"R\xC3\xB4le", roleName(v.role)});
        rows.push_back({"Taille", std::to_string(v.width) + " \xC3\x97 " + std::to_string(v.height) + " px"});
        if (const View* t = v.templateView != kNoId ? p.view(v.templateView) : nullptr) rows.push_back({"\xC3\x89" "cran mod\xC3\xA8le", t->name});
        if (const View* up = v.upView != kNoId ? p.view(v.upView) : nullptr) rows.push_back({"Vue parente", up->name});
        rows.push_back({"Objets", std::to_string(v.objects.size()) + " : " + kindsOf(v)});
        std::size_t expressions = 0, actions = v.actions.size();
        for (const auto& o : v.objects) {
            actions += o.actions.size();
            for (const auto& pr : o.props) expressions += !pr.expr.empty();
        }
        rows.push_back({"Animations et actions", std::to_string(expressions) + " expression(s), " + std::to_string(actions) + " action(s)"});
        if (!v.scripts.empty()) {
            std::vector<std::string> names;
            for (const auto& s : v.scripts) names.push_back(s.name + " (" + std::string(eventLabel(s.event)) + ")");
            rows.push_back({"Scripts de la vue", joinWith(names, ", ")});
        }
        const auto targets = targetsOf(p, v);
        rows.push_back({"M\xC3\xA8ne \xC3\xA0", targets.empty() ? std::string("-") : joinWith(targets, ", ")});
        if (v.role == "popup" && !v.params.empty()) {
            std::vector<std::string> params;
            // 1.9 : le type et le mode (seulement s'ils disent quelque chose)
            for (const auto& pr : v.params)
                params.push_back(pr.name + (pr.type.empty() ? std::string{} : " : " + pr.type)
                                 + (pr.mode == ParamMode::Reference ? std::string{} : " (" + std::string(params::paramModeBadge(pr.mode)) + ")")
                                 + (pr.defaultValue.empty() ? std::string{} : " := " + pr.defaultValue));
            rows.push_back({"Param\xC3\xA8tres", joinWith(params, " ; ")});
        }
        w.table({"", ""}, std::move(rows), {0.27, 0.73});
    };
    for (const char* role : {"vue", "popup"})
        for (const auto& v : p.views)
            if (v.role == role) describe(v);
    if (templates > 0) {
        w.add(K::Heading2, "Les \xC3\xA9" "crans mod\xC3\xA8les, en-t\xC3\xAAtes et pieds de page");
        std::vector<std::vector<std::string>> rows;
        for (const auto& v : p.views)
            if (v.role == "modele" || v.role == "entete" || v.role == "pied")
                rows.push_back({v.name, roleName(v.role), std::to_string(v.width) + " \xC3\x97 " + std::to_string(v.height), std::to_string(v.objects.size())});
        w.table({"Vue", "R\xC3\xB4le", "Taille", "Objets"}, std::move(rows), {0.4, 0.25, 0.2, 0.15});
    }
    if (symbols > 0) {
        w.add(K::Heading2, "Les symboles");
        std::vector<std::vector<std::string>> rows;
        for (const auto& v : p.views) {
            if (!isSymbolView(v)) continue;
            std::size_t uses = 0;
            for (const auto& other : p.views)
                for (const auto& o : other.objects) uses += o.kind == Kind::SymbolInstance && o.text("symbol") == v.name;
            std::vector<std::string> params;
            for (const auto& pr : v.params) params.push_back(pr.name);
            rows.push_back({v.name, joinWith(params, ", "), std::to_string(v.objects.size()), std::to_string(uses)});
        }
        w.table({"Symbole", "Param\xC3\xA8tres", "Objets", "Instances"}, std::move(rows), {0.34, 0.36, 0.14, 0.16});
    }

    // ---- 3. les variables IHM
    w.add(K::PageBreak, {});
    w.add(K::Heading1, sections[2]);
    if (p.programs.variables.empty()) {
        w.add(K::Paragraph, "Aucune variable IHM : l'IHM lit et \xC3\xA9" "crit les variables de l'automate.");
    } else {
        // Lot 16 : le dossier, le type (structure, tableau), la liaison.
        std::vector<std::vector<std::string>> rows;
        for (const auto& v : p.programs.variables) {
            std::string link = v.bound() ? v.equipment + " " + v.address : std::string("-");
            if (v.bound() && types::isComposite(v.type))
                if (const auto span = types::spanText(p, v); !span.empty()) link = v.equipment + " : " + span;
            rows.push_back({v.name, v.folder.empty() ? std::string("-") : v.folder, v.type, v.initial.empty() ? std::string("-") : v.initial,
                            link, v.description});
        }
        w.table({"Variable", "Dossier", "Type", "Valeur initiale", "Liaison", "Description"}, std::move(rows), {0.17, 0.12, 0.19, 0.12, 0.18, 0.22});
    }
    if (!p.programs.types.empty()) {
        w.add(K::Heading2, "Les types IHM (structures)");
        for (const auto& ty : p.programs.types) {
            w.add(K::Paragraph, types::summary(p, ty.name) + (ty.description.empty() ? std::string{} : " \xE2\x80\x94 " + ty.description));
            std::vector<std::vector<std::string>> rows;
            for (const auto& m : ty.members)
                rows.push_back({m.name, m.type, m.initial.empty() ? std::string("-") : m.initial, m.description});
            w.table({"Membre", "Type", "Valeur initiale", "Description"}, std::move(rows), {0.24, 0.28, 0.16, 0.32});
        }
    }
    // Lot 13 : les unites et les formats, repris partout ou la variable s'affiche.
    if (!p.displays.empty()) {
        w.add(K::Heading2, "Les unit\xC3\xA9s et les formats");
        std::vector<std::vector<std::string>> rows;
        for (const auto& vd : p.displays)
            rows.push_back({vd.path, vd.unit, vd.format.empty() ? std::string("-") : vd.format, displaySample(vd),
                            std::to_string(displayUses(p, vd).size())});
        w.table({"Variable", "Unit\xC3\xA9", "Format", "Exemple", "Objets"}, std::move(rows), {0.4, 0.13, 0.13, 0.22, 0.12});
    }

    // ---- 4. les alarmes
    w.add(K::Heading1, sections[3]);
    if (p.alarms.empty()) {
        w.add(K::Paragraph, "Aucune alarme d\xC3\xA9" "finie.");
    } else {
        std::vector<std::vector<std::string>> rows;
        for (const auto& a : p.alarms)
            rows.push_back({a.name, std::to_string(a.priority) + " - " + std::string(alarmPriorityLabel(a.priority)), a.category, a.group,
                            a.condition, a.message, a.ackRequired ? "requis" : "automatique"});
        w.table({"Alarme", "Priorit\xC3\xA9", "Cat\xC3\xA9gorie", "Groupe", "Condition", "Message", "Acquittement"}, std::move(rows),
                {0.14, 0.1, 0.11, 0.09, 0.19, 0.23, 0.14});
        std::vector<std::vector<std::string>> instructions;
        for (const auto& a : p.alarms)
            if (!a.instruction.empty()) instructions.push_back({a.name, a.instruction});
        if (!instructions.empty()) {
            w.add(K::Heading2, "Les consignes");
            w.table({"Alarme", "Ce que fait l'op\xC3\xA9rateur"}, std::move(instructions), {0.28, 0.72});
        }
    }

    // ---- 5. les recettes
    w.add(K::Heading1, sections[4]);
    if (p.recipes.empty()) w.add(K::Paragraph, "Aucune recette.");
    for (const auto& r : p.recipes) {
        w.add(K::Heading2, r.name);
        if (!r.description.empty()) w.add(K::Paragraph, r.description);
        std::vector<std::vector<std::string>> fields;
        for (const auto& f : r.fields) fields.push_back({f.name, f.variable, f.unit, f.min, f.max});
        w.table({"\xC3\x89l\xC3\xA9ment", "Variable", "Unit\xC3\xA9", "Min", "Max"}, std::move(fields), {0.26, 0.4, 0.12, 0.11, 0.11});
        if (!r.records.empty()) {
            std::vector<std::string> headers{"Jeu"};
            for (const auto& f : r.fields) headers.push_back(f.name + (f.unit.empty() ? std::string{} : " (" + f.unit + ")"));
            std::vector<std::vector<std::string>> rows;
            for (const auto& rec : r.records) {
                std::vector<std::string> row{rec.name};
                for (const auto& val : rec.values) row.push_back(val);
                rows.push_back(std::move(row));
            }
            w.table(std::move(headers), std::move(rows));
        }
    }

    // ---- 6. les utilisateurs et la securite
    w.add(K::PageBreak, {});
    w.add(K::Heading1, sections[5]);
    const auto& sec = p.security;
    w.add(K::Paragraph, sec.enabled ? "La s\xC3\xA9" "curit\xC3\xA9 est active : chaque geste demande sa permission, chaque objet son niveau."
                                    : "La s\xC3\xA9" "curit\xC3\xA9 est inactive : tout est permis \xC3\xA0 tout le monde.");
    {
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Utilisateur au lancement", sec.startUser.empty() ? std::string("personne") : sec.startUser});
        rows.push_back({"D\xC3\xA9" "connexion automatique", sec.autoLogoutMin > 0 ? std::to_string(sec.autoLogoutMin) + " min, avertie " + std::to_string(sec.logoutWarnS) + " s avant" : std::string("jamais")});
        std::string rules = std::to_string(std::max(0, sec.pwMinLength)) + " caract\xC3\xA8res au moins";
        if (sec.pwDigit) rules += ", un chiffre";
        if (sec.pwLetter) rules += ", une lettre";
        if (sec.pwMixedCase) rules += ", majuscules et minuscules";
        if (sec.pwSpecial) rules += ", un caract\xC3\xA8re sp\xC3\xA9" "cial";
        rows.push_back({"Mots de passe", sec.pwMinLength > 0 || sec.pwDigit || sec.pwLetter || sec.pwMixedCase || sec.pwSpecial ? rules : std::string("pas de r\xC3\xA8gle")});
        rows.push_back({"P\xC3\xA9remption", sec.pwMaxAgeDays > 0 ? std::to_string(sec.pwMaxAgeDays) + " jours" : std::string("jamais")});
        rows.push_back({"Derniers interdits", sec.pwHistory > 0 ? std::to_string(sec.pwHistory) : std::string("-")});
        rows.push_back({"\xC3\x80 changer \xC3\xA0 la premi\xC3\xA8re connexion", yesNo(sec.pwChangeFirst)});
        rows.push_back({"Verrouillage", sec.lockAttempts > 0 ? std::to_string(sec.lockAttempts) + " \xC3\xA9" "checs, " + (sec.lockMinutes > 0 ? std::to_string(sec.lockMinutes) + " min" : std::string("jusqu'\xC3\xA0 un administrateur")) : std::string("jamais")});
        rows.push_back({"Connexion par badge", yesNo(sec.badgeLogin)});
        w.table({"R\xC3\xA8gle", "Valeur"}, std::move(rows), {0.4, 0.6});
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& g : sec.groups) {
            const View* start = g.startView != kNoId ? p.view(g.startView) : nullptr;
            rows.push_back({g.name, std::to_string(g.level), joinWith(g.roles, ", "), start ? start->name : std::string("-")});
        }
        w.add(K::Heading2, "Les groupes");
        w.table({"Groupe", "Niveau", "R\xC3\xB4les", "Vue de d\xC3\xA9marrage"}, std::move(rows), {0.26, 0.1, 0.4, 0.24});
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : sec.roles) rows.push_back({r.name, joinWith(r.permissions, ", ")});
        w.add(K::Heading2, "Les r\xC3\xB4les");
        w.table({"R\xC3\xB4le", "Permissions"}, std::move(rows), {0.3, 0.7});
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& u : sec.users) {
            const UserGroup* g = nullptr;
            for (const auto& x : sec.groups) if (x.id == u.group) g = &x;
            rows.push_back({u.login, u.fullName, g ? g->name : std::string("-"), u.protection, yesNo(u.enabled), yesNo(!u.badge.empty())});
        }
        w.add(K::Heading2, "Les comptes");
        if (rows.empty()) w.add(K::Paragraph, "Aucun compte.");
        else w.table({"Identifiant", "Nom", "Groupe", "Protection", "Actif", "Badge"}, std::move(rows), {0.18, 0.26, 0.18, 0.14, 0.12, 0.12});
    }

    // ---- 7. les scripts et les fonctions
    w.add(K::PageBreak, {});
    w.add(K::Heading1, sections[6]);
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& s : p.programs.scripts) rows.push_back({s.name, std::string(scriptLangKey(s.lang)), eventName(s), s.description});
        for (const auto& v : p.views)
            for (const auto& s : v.scripts) rows.push_back({v.name + "." + s.name, std::string(scriptLangKey(s.lang)), eventName(s), s.description});
        if (rows.empty()) w.add(K::Paragraph, "Aucun script.");
        else w.table({"Script", "Langage", "\xC3\x89v\xC3\xA9nement", "Description"}, std::move(rows), {0.3, 0.1, 0.25, 0.35});
    }
    if (!p.programs.functions.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& f : p.programs.functions) rows.push_back({f.name, f.returnType.empty() ? std::string("(sans retour)") : f.returnType, f.description});
        w.add(K::Heading2, "Les fonctions IHM");
        w.table({"Fonction", "Retour", "Description"}, std::move(rows), {0.3, 0.18, 0.52});
    }
    if (opt.code) {
        for (const auto& s : p.programs.scripts) {
            w.add(K::Heading2, "Script " + s.name);
            w.add(K::Code, s.body);
        }
        for (const auto& v : p.views)
            for (const auto& s : v.scripts) {
                w.add(K::Heading2, "Script " + v.name + "." + s.name);
                w.add(K::Code, s.body);
            }
        for (const auto& f : p.programs.functions) {
            w.add(K::Heading2, "Fonction " + f.name);
            w.add(K::Code, f.body);
        }
    }

    // ---- 8. les historiques
    w.add(K::Heading1, sections[7]);
    {
        const auto& h = p.history;
        w.table({"Ce qui est gard\xC3\xA9", "R\xC3\xA9glage"},
                {{"Alarmes termin\xC3\xA9" "es", yesNo(h.alarms)},
                 {"\xC3\x89v\xC3\xA9nements", yesNo(h.events)},
                 {"Journal syst\xC3\xA8me", yesNo(h.system)},
                 {"Journal d'audit (cha\xC3\xAEn\xC3\xA9)", yesNo(h.audit)},
                 {"Entr\xC3\xA9" "es par liste", std::to_string(h.maxEntries)},
                 {"Conservation", std::to_string(h.retentionDays) + " jours"},
                 {"\xC3\x89" "chantillonnage des mesures", std::to_string(h.samplePeriodMs) + " ms"},
                 {"Variables archiv\xC3\xA9" "es", h.archived.empty() ? std::string("-") : joinWith(h.archived, " ; ")}},
                {0.4, 0.6});
    }

    // ---- 9. les essais de reception
    w.add(K::Heading1, sections[8]);
    if (p.scenarios.empty()) {
        w.add(K::Paragraph, "Aucun essai de r\xC3\xA9" "ception (IHM > Essais).");
    } else {
        std::vector<std::vector<std::string>> rows;
        for (const auto& sc : p.scenarios) {
            std::string last = "-";
            if (opt.reports)
                if (const auto it = opt.reports->find(sc.id); it != opt.reports->end() && it->second.done)
                    last = (it->second.ok() ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec") + std::string(" - ") + it->second.summary();
            rows.push_back({sc.name, std::to_string(sc.steps.size()), sc.description, last});
        }
        w.table({"Essai", "Pas", "Ce qu'il d\xC3\xA9montre", "Dernier passage"}, std::move(rows), {0.22, 0.08, 0.4, 0.3});
        for (const auto& sc : p.scenarios) {
            w.add(K::Heading2, "Essai " + sc.name);
            std::vector<std::vector<std::string>> steps;
            const ScenarioReport* rep = nullptr;
            if (opt.reports)
                if (const auto it = opt.reports->find(sc.id); it != opt.reports->end() && it->second.steps.size() == sc.steps.size()) rep = &it->second;
            for (std::size_t i = 0; i < sc.steps.size(); ++i)
                steps.push_back({std::to_string(i + 1), describeStep(sc.steps[i]),
                                 rep && rep->steps[i].verdict != Verdict::Pending ? std::string(verdictLabel(rep->steps[i].verdict)) : std::string("-")});
            w.table({"N\xC2\xB0", "Pas", "Verdict"}, std::move(steps), {0.08, 0.74, 0.18});
        }
    }

    // ---- 10. les ressources et les styles
    w.add(K::Heading1, sections[9]);
    if (p.assets.resources.empty()) {
        w.add(K::Paragraph, "Aucune ressource.");
    } else {
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : p.assets.resources)
            rows.push_back({r.name, std::string(mediaKindLabel(r.kind())), r.format,
                            r.width > 0 ? std::to_string(r.width) + " \xC3\x97 " + std::to_string(r.height) : std::string("-"), formatBytes(r.bytes)});
        w.table({"Ressource", "Genre", "Format", "Taille", "Poids"}, std::move(rows), {0.36, 0.16, 0.12, 0.18, 0.18});
    }
    if (!p.styles.empty()) {
        w.add(K::Heading2, "Les styles nomm\xC3\xA9s");
        std::vector<std::vector<std::string>> rows;
        for (const auto& s : p.styles) {
            std::size_t uses = 0;
            for (const auto& v : p.views)
                for (const auto& o : v.objects) uses += o.text("namedStyle") == s.name;
            std::vector<std::string> props;
            for (const auto& pr : s.props) props.push_back(pr.key + " = " + pr.value);
            rows.push_back({s.name, joinWith(props, " ; "), std::to_string(uses)});
        }
        w.table({"Style", "Valeurs", "Objets"}, std::move(rows), {0.25, 0.6, 0.15});
    }
    return d;
}

// ======================================================================= JPEG ===
bool jpegSize(const Bytes& b, int& width, int& height) {
    width = height = 0;
    if (b.size() < 4 || b[0] != 0xFF || b[1] != 0xD8) return false;
    std::size_t i = 2;
    while (i + 9 < b.size()) {
        if (b[i] != 0xFF) { ++i; continue; }
        const std::uint8_t marker = b[i + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
        const std::size_t len = (static_cast<std::size_t>(b[i + 2]) << 8) | b[i + 3];
        if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) || (marker >= 0xC9 && marker <= 0xCB)
            || (marker >= 0xCD && marker <= 0xCF)) {
            height = (b[i + 5] << 8) | b[i + 6];
            width = (b[i + 7] << 8) | b[i + 8];
            return width > 0 && height > 0;
        }
        if (len < 2) return false;
        i += 2 + len;
    }
    return false;
}

// ======================================================================= Word ===
namespace {

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                // Les caracteres de controle n'ont pas leur place dans le XML (sauf la tabulation).
                if (static_cast<unsigned char>(c) < 32 && c != '\t') out += ' ';
                else out += c;
        }
    }
    return out;
}

std::string run(std::string_view text, bool bold = false) {
    std::string r = "<w:r>";
    if (bold) r += "<w:rPr><w:b/></w:rPr>";
    return r + "<w:t xml:space=\"preserve\">" + xmlEscape(text) + "</w:t></w:r>";
}

std::string paragraph(std::string_view style, std::string_view text, bool bold = false) {
    std::string p = "<w:p>";
    if (!style.empty()) p += "<w:pPr><w:pStyle w:val=\"" + std::string(style) + "\"/></w:pPr>";
    return p + run(text, bold) + "</w:p>";
}

constexpr int kTextTwips = 11906 - 2 * 1134;       // A4, marges de 2 cm

std::vector<double> tableWidths(const DossierBlock& t, std::size_t n) {
    std::vector<double> w = t.widths;
    if (w.size() != n) {
        // Mesurees : la plus longue cellule de chaque colonne (bornee), en proportions.
        w.assign(n, 1.0);
        for (std::size_t c = 0; c < n; ++c) {
            double m = c < t.headers.size() ? static_cast<double>(t.headers[c].size()) : 4.0;
            for (const auto& r : t.rows) if (c < r.size()) m = std::max(m, std::min(60.0, static_cast<double>(r[c].size())));
            w[c] = std::max(4.0, m);
        }
    }
    double sum = 0;
    for (const double x : w) sum += x;
    for (auto& x : w) x = sum > 0 ? x / sum : 1.0 / static_cast<double>(n);
    return w;
}

} // namespace

Bytes dossierDocx(const Dossier& d) {
    std::string body;
    std::string rels;
    std::vector<std::pair<std::string, std::string>> media;
    int pictureId = 1;
    for (const auto& b : d.blocks) {
        switch (b.kind) {
            case K::Title: body += paragraph("Title", b.text); break;
            case K::Subtitle: body += paragraph("Subtitle", b.text); break;
            case K::Heading1: body += paragraph("Heading1", b.text); break;
            case K::Heading2: body += paragraph("Heading2", b.text); break;
            case K::Paragraph: body += paragraph({}, b.text); break;
            case K::Note: body += paragraph("Note", b.text); break;
            case K::PageBreak: body += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"; break;
            case K::Code: {
                std::istringstream in(b.text);
                std::string line;
                bool any = false;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    std::string expanded;
                    for (const char c : line) expanded += c == '\t' ? std::string("    ") : std::string(1, c);
                    body += paragraph("Code", expanded);
                    any = true;
                }
                if (!any) body += paragraph("Code", "(vide)");
                break;
            }
            case K::Image: {
                if (b.image < 0 || static_cast<std::size_t>(b.image) >= d.images.size()) break;
                const auto& img = d.images[static_cast<std::size_t>(b.image)];
                int w = img.width, h = img.height;
                if ((w <= 0 || h <= 0) && !jpegSize(img.jpeg, w, h)) break;
                const std::string file = "image" + std::to_string(pictureId) + ".jpeg";
                const std::string rid = "rIdImg" + std::to_string(pictureId);
                media.emplace_back("word/media/" + file, std::string(img.jpeg.begin(), img.jpeg.end()));
                rels += "<Relationship Id=\"" + rid + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/"
                      + file + "\"/>";
                // 16 cm de large au plus, 11 cm de haut au plus.
                const double maxW = 16.0 * 360000.0, maxH = 11.0 * 360000.0;
                double cx = maxW, cy = maxW * static_cast<double>(h) / static_cast<double>(w);
                if (cy > maxH) { cy = maxH; cx = maxH * static_cast<double>(w) / static_cast<double>(h); }
                const std::string ext = "cx=\"" + std::to_string(static_cast<long long>(cx)) + "\" cy=\"" + std::to_string(static_cast<long long>(cy)) + "\"";
                const std::string id = std::to_string(pictureId);
                body += "<w:p><w:pPr><w:pStyle w:val=\"Figure\"/></w:pPr><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent " + ext
                      + "/><wp:docPr id=\"" + id + "\" name=\"" + xmlEscape(img.name) + "\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                        "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"" + id + "\" name=\"" + file + "\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"" + rid
                      + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext " + ext
                      + "/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:ln w=\"6350\"><a:solidFill><a:srgbClr val=\"A6A6A6\"/></a:solidFill></a:ln></pic:spPr></pic:pic>"
                        "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>";
                if (!img.caption.empty()) body += paragraph("Caption", img.caption);
                ++pictureId;
                break;
            }
            case K::Table: {
                std::size_t n = b.headers.size();
                for (const auto& r : b.rows) n = std::max(n, r.size());
                if (n == 0) break;
                const auto widths = tableWidths(b, n);
                const bool header = std::any_of(b.headers.begin(), b.headers.end(), [](const std::string& h) { return !h.empty(); });
                body += "<w:tbl><w:tblPr><w:tblStyle w:val=\"Grille\"/><w:tblW w:w=\"5000\" w:type=\"pct\"/><w:tblLayout w:type=\"fixed\"/></w:tblPr><w:tblGrid>";
                for (const double x : widths) body += "<w:gridCol w:w=\"" + std::to_string(static_cast<int>(x * kTextTwips)) + "\"/>";
                body += "</w:tblGrid>";
                const auto cell = [&](const std::string& text, std::size_t c, bool head, bool firstColumn) {
                    std::string tc = "<w:tc><w:tcPr><w:tcW w:w=\"" + std::to_string(static_cast<int>(widths[c] * kTextTwips)) + "\" w:type=\"dxa\"/>";
                    if (head) tc += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"DCE6F2\"/>";
                    else if (firstColumn && !header) tc += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F4F7\"/>";
                    tc += "</w:tcPr>";
                    std::istringstream in(text);
                    std::string line;
                    bool any = false;
                    while (std::getline(in, line)) {
                        tc += "<w:p><w:pPr><w:pStyle w:val=\"Table\"/></w:pPr>" + run(line, head || (firstColumn && !header)) + "</w:p>";
                        any = true;
                    }
                    if (!any) tc += "<w:p><w:pPr><w:pStyle w:val=\"Table\"/></w:pPr></w:p>";
                    return tc + "</w:tc>";
                };
                if (header) {
                    body += "<w:tr><w:trPr><w:tblHeader/><w:cantSplit/></w:trPr>";
                    for (std::size_t c = 0; c < n; ++c) body += cell(c < b.headers.size() ? b.headers[c] : std::string{}, c, true, false);
                    body += "</w:tr>";
                }
                for (const auto& r : b.rows) {
                    body += "<w:tr><w:trPr><w:cantSplit/></w:trPr>";
                    for (std::size_t c = 0; c < n; ++c) body += cell(c < r.size() ? r[c] : std::string{}, c, false, c == 0);
                    body += "</w:tr>";
                }
                body += "</w:tbl><w:p><w:pPr><w:pStyle w:val=\"Spacer\"/></w:pPr></w:p>";
                break;
            }
        }
    }
    const std::string ns =
        " xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"";
    const std::string document = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document" + ns + "><w:body>" + body
                               + "<w:sectPr><w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
                                 "<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"567\" w:footer=\"567\" w:gutter=\"0\"/></w:sectPr>"
                                 "</w:body></w:document>";
    const std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:cs=\"Calibri\"/><w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/>"
        "<w:lang w:val=\"fr-FR\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"80\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
        "<w:pPr><w:spacing w:before=\"1200\" w:after=\"200\"/></w:pPr><w:rPr><w:b/><w:color w:val=\"1F3864\"/><w:sz w:val=\"52\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Subtitle\"><w:name w:val=\"Subtitle\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
        "<w:pPr><w:spacing w:after=\"480\"/></w:pPr><w:rPr><w:color w:val=\"595959\"/><w:sz w:val=\"28\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
        "<w:pPr><w:keepNext/><w:spacing w:before=\"360\" w:after=\"160\"/><w:pBdr><w:bottom w:val=\"single\" w:sz=\"8\" w:space=\"4\" w:color=\"2F5496\"/></w:pBdr>"
        "<w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"32\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
        "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"100\"/><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"25\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Note\"><w:name w:val=\"Note\"/><w:basedOn w:val=\"Normal\"/><w:rPr><w:i/><w:color w:val=\"6B7280\"/><w:sz w:val=\"18\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Caption\"><w:name w:val=\"caption\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:jc w:val=\"center\"/><w:spacing w:after=\"200\"/></w:pPr>"
        "<w:rPr><w:i/><w:color w:val=\"6B7280\"/><w:sz w:val=\"18\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Figure\"><w:name w:val=\"Figure\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:keepNext/><w:jc w:val=\"center\"/><w:spacing w:before=\"80\" w:after=\"40\"/></w:pPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Code\"><w:name w:val=\"Code\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
        "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F3F4F6\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/><w:sz w:val=\"16\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Table\"><w:name w:val=\"Table Text\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
        "<w:rPr><w:sz w:val=\"18\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Spacer\"><w:name w:val=\"Spacer\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"120\"/></w:pPr><w:rPr><w:sz w:val=\"8\"/></w:rPr></w:style>"
        "<w:style w:type=\"table\" w:styleId=\"Grille\"><w:name w:val=\"Grille\"/><w:tblPr><w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/><w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/><w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/><w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BFBFBF\"/>"
        "</w:tblBorders><w:tblCellMar><w:top w:w=\"40\" w:type=\"dxa\"/><w:left w:w=\"80\" w:type=\"dxa\"/><w:bottom w:w=\"40\" w:type=\"dxa\"/><w:right w:w=\"80\" w:type=\"dxa\"/></w:tblCellMar>"
        "</w:tblPr></w:style>"
        "</w:styles>";
    const std::string field = [](const char* code) {
        return std::string("<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> ") + code
             + " </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>";
    }("PAGE");
    const std::string pages = [](const char* code) {
        return std::string("<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> ") + code
             + " </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>";
    }("NUMPAGES");
    const std::string footer = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:ftr" + ns + "><w:p><w:pPr><w:pStyle w:val=\"Note\"/>"
                               "<w:tabs><w:tab w:val=\"right\" w:pos=\"9638\"/></w:tabs></w:pPr>" + run(d.title)
                             + "<w:r><w:tab/></w:r>" + run("Page ") + field + run(" / ") + pages + "</w:p></w:ftr>";
    const std::string docRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                "<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
                                "<Relationship Id=\"rIdFooter\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>"
                              + rels + "</Relationships>";
    const std::string rootRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                 "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                                 "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                                 "</Relationships>";
    const std::string types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                              "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                              "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                              "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                              "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
                              "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                              "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
                              "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>"
                              "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                              "</Types>";
    const std::string core = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                             "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                             "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                             "<dc:title>" + xmlEscape(d.title) + "</dc:title><dc:creator>" + xmlEscape(d.author.empty() ? std::string("XpgAnalyzer") : d.author)
                           + "</dc:creator></cp:coreProperties>";
    std::vector<std::pair<std::string, std::string>> items{{"[Content_Types].xml", types},
                                                           {"_rels/.rels", rootRels},
                                                           {"docProps/core.xml", core},
                                                           {"word/document.xml", document},
                                                           {"word/styles.xml", styles},
                                                           {"word/footer1.xml", footer},
                                                           {"word/_rels/document.xml.rels", docRels}};
    for (auto& m : media) items.push_back(std::move(m));
    return zipStored(items);
}

// ======================================================================== PDF ===
namespace {

constexpr double kPageW = 595.28, kPageH = 841.89;
constexpr double kLeft = 56, kRight = 56, kTop = 64, kBottom = 64;
constexpr double kTextW = kPageW - kLeft - kRight;

enum class Font : std::uint8_t { Regular, Bold, Mono, Italic };

double widthOf(std::string_view s, double size, Font f) {
    if (f == Font::Mono) return static_cast<double>(s.size()) * 0.6 * size;
    return pdfTextWidth(s, size, f == Font::Bold);
}
const char* fontName(Font f) {
    switch (f) {
        case Font::Regular: return "/F1";
        case Font::Bold: return "/F2";
        case Font::Mono: return "/F3";
        case Font::Italic: return "/F4";
    }
    return "/F1";
}

// Des lignes de `maxW` points au plus : coupees aux espaces ; un mot trop long, au caractere.
std::vector<std::string> wrap(const std::string& winAnsi, double size, Font f, double maxW) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= winAnsi.size()) {
        const auto nl = winAnsi.find('\n', from);
        const std::string para = winAnsi.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        std::string line;
        std::size_t i = 0;
        if (para.empty()) out.emplace_back();
        while (i < para.size()) {
            std::size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            std::string word = para.substr(i, j - i);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (widthOf(candidate, size, f) <= maxW) {
                line = candidate;
            } else {
                if (!line.empty()) out.push_back(line);
                line.clear();
                while (widthOf(word, size, f) > maxW && word.size() > 1) {
                    std::size_t k = 1;
                    while (k < word.size() && widthOf(word.substr(0, k + 1), size, f) <= maxW) ++k;
                    out.push_back(word.substr(0, k));
                    word = word.substr(k);
                }
                line = word;
            }
            i = j + 1;
        }
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    if (out.empty()) out.emplace_back();
    return out;
}

struct Outline { std::string title; int level{0}; std::size_t page{0}; double y{0}; };

struct Pdf {
    std::vector<std::string> pages;          // les flux de contenu
    std::vector<std::vector<int>> pageImages;
    std::vector<Outline> outline;
    double y{kPageH - kTop};
    void newPage() {
        pages.emplace_back();
        pageImages.emplace_back();
        y = kPageH - kTop;
    }
    std::string& s() { return pages.back(); }
    void ensure(double h) {
        if (pages.empty() || y - h < kBottom) newPage();
    }
    void text(double x, double baseline, const std::string& winAnsi, double size, Font f, double r = 0.1, double g = 0.1, double b = 0.12) {
        s() += "BT " + std::string(fontName(f)) + " " + pdfNumber(size) + " Tf " + pdfNumber(r) + " " + pdfNumber(g) + " " + pdfNumber(b) + " rg "
             + pdfNumber(x) + " " + pdfNumber(baseline) + " Td " + pdfLiteral(winAnsi) + " Tj ET\n";
    }
    void rect(double x, double y0, double w, double h, double r, double g, double b) {
        s() += pdfNumber(r) + " " + pdfNumber(g) + " " + pdfNumber(b) + " rg " + pdfNumber(x) + " " + pdfNumber(y0) + " " + pdfNumber(w) + " " + pdfNumber(h) + " re f\n";
    }
    void line(double x0, double y0, double x1, double y1, double gray, double width) {
        s() += pdfNumber(gray) + " G " + pdfNumber(width) + " w " + pdfNumber(x0) + " " + pdfNumber(y0) + " m " + pdfNumber(x1) + " " + pdfNumber(y1) + " l S\n";
    }
    // Un paragraphe ; rend la hauteur prise.
    void paragraph(const std::string& utf8, double size, Font f, double gap, double r = 0.1, double g = 0.1, double b = 0.12, double indent = 0) {
        const auto lines = wrap(toWinAnsi(utf8), size, f, kTextW - indent);
        const double lh = size * 1.32;
        for (const auto& l : lines) {
            ensure(lh);
            y -= lh;
            text(kLeft + indent, y + size * 0.28, l, size, f, r, g, b);
        }
        y -= gap;
    }
};

} // namespace

Bytes dossierPdf(const Dossier& d) {
    Pdf pdf;
    pdf.newPage();
    // Un titre ne reste pas seul en bas de page : la place du bloc qui le suit
    // (une image, le debut d'un tableau, un paragraphe) avec lui.
    const auto nextHeight = [&](std::size_t i) -> double {
        if (i + 1 >= d.blocks.size()) return 0;
        const auto& n = d.blocks[i + 1];
        if (n.kind == K::Image && n.image >= 0 && static_cast<std::size_t>(n.image) < d.images.size()) {
            const auto& img = d.images[static_cast<std::size_t>(n.image)];
            int w = img.width, h = img.height;
            if ((w <= 0 || h <= 0) && !jpegSize(img.jpeg, w, h)) return 0;
            return std::min(300.0, kTextW * static_cast<double>(h) / static_cast<double>(w)) + 40;
        }
        if (n.kind == K::Table) return 48;
        if (n.kind == K::Code) return 40;
        return 26;
    };
    for (std::size_t bi = 0; bi < d.blocks.size(); ++bi) {
        const auto& b = d.blocks[bi];
        switch (b.kind) {
            case K::Title:
                pdf.y -= 150;
                pdf.paragraph(b.text, 26, Font::Bold, 10, 0.12, 0.22, 0.39);
                pdf.outline.push_back({b.text, 0, pdf.pages.size() - 1, pdf.y + 40});
                break;
            case K::Subtitle:
                pdf.paragraph(b.text, 13, Font::Regular, 26, 0.35, 0.35, 0.38);
                break;
            case K::Heading1: {
                pdf.ensure(60 + nextHeight(bi));
                pdf.y -= 14;
                pdf.outline.push_back({b.text, 0, pdf.pages.size() - 1, pdf.y + 20});
                pdf.paragraph(b.text, 16, Font::Bold, 4, 0.18, 0.33, 0.59);
                pdf.line(kLeft, pdf.y + 2, kPageW - kRight, pdf.y + 2, 0.55, 0.8);
                pdf.y -= 10;
                break;
            }
            case K::Heading2:
                pdf.ensure(34 + nextHeight(bi));
                pdf.y -= 8;
                pdf.outline.push_back({b.text, 1, pdf.pages.size() - 1, pdf.y + 16});
                pdf.paragraph(b.text, 12.5, Font::Bold, 5, 0.18, 0.33, 0.59);
                break;
            case K::Paragraph:
                pdf.paragraph(b.text, 10, Font::Regular, 6);
                break;
            case K::Note:
                pdf.paragraph(b.text, 9, Font::Italic, 6, 0.42, 0.45, 0.5);
                break;
            case K::PageBreak:
                pdf.newPage();
                break;
            case K::Code: {
                const double size = 7.6, lh = size * 1.3;
                std::vector<std::string> lines;
                std::istringstream in(b.text);
                std::string raw;
                const std::size_t perLine = static_cast<std::size_t>((kTextW - 12) / (0.6 * size));
                while (std::getline(in, raw)) {
                    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
                    std::string expanded;
                    for (const char c : raw) expanded += c == '\t' ? std::string("    ") : std::string(1, c);
                    std::string w = toWinAnsi(expanded);
                    if (w.empty()) lines.emplace_back();
                    while (!w.empty()) {
                        lines.push_back(w.substr(0, perLine));
                        w = w.size() > perLine ? w.substr(perLine) : std::string{};
                    }
                }
                if (lines.empty()) lines.emplace_back("(vide)");
                std::size_t i = 0;
                while (i < lines.size()) {
                    pdf.ensure(lh + 8);
                    // Ce qui tient sur la page, sur un fond gris.
                    const std::size_t fit = std::max<std::size_t>(1, static_cast<std::size_t>((pdf.y - kBottom - 8) / lh));
                    const std::size_t n = std::min(fit, lines.size() - i);
                    const double h = static_cast<double>(n) * lh + 8;
                    pdf.rect(kLeft, pdf.y - h, kTextW, h, 0.95, 0.955, 0.965);
                    double yy = pdf.y - 4;
                    for (std::size_t k = 0; k < n; ++k) {
                        yy -= lh;
                        pdf.text(kLeft + 6, yy + size * 0.28, lines[i + k], size, Font::Mono, 0.12, 0.14, 0.18);
                    }
                    pdf.y -= h;
                    i += n;
                    if (i < lines.size()) pdf.newPage();
                }
                pdf.y -= 8;
                break;
            }
            case K::Image: {
                if (b.image < 0 || static_cast<std::size_t>(b.image) >= d.images.size()) break;
                const auto& img = d.images[static_cast<std::size_t>(b.image)];
                int w = img.width, h = img.height;
                if ((w <= 0 || h <= 0) && !jpegSize(img.jpeg, w, h)) break;
                double iw = kTextW, ih = kTextW * static_cast<double>(h) / static_cast<double>(w);
                if (ih > 300) { ih = 300; iw = 300 * static_cast<double>(w) / static_cast<double>(h); }
                pdf.ensure(ih + 24);
                const double x = kLeft + (kTextW - iw) / 2, y0 = pdf.y - ih - 4;
                pdf.s() += "q " + pdfNumber(iw) + " 0 0 " + pdfNumber(ih) + " " + pdfNumber(x) + " " + pdfNumber(y0) + " cm /Im" + std::to_string(b.image) + " Do Q\n";
                pdf.s() += "0.65 G 0.5 w " + pdfNumber(x) + " " + pdfNumber(y0) + " " + pdfNumber(iw) + " " + pdfNumber(ih) + " re S\n";
                pdf.pageImages.back().push_back(b.image);
                pdf.y = y0 - 4;
                if (!img.caption.empty()) {
                    const std::string cap = toWinAnsi(img.caption);
                    pdf.y -= 11;
                    pdf.text(kLeft + (kTextW - widthOf(cap, 8.5, Font::Italic)) / 2, pdf.y + 2, cap, 8.5, Font::Italic, 0.42, 0.45, 0.5);
                }
                pdf.y -= 10;
                break;
            }
            case K::Table: {
                std::size_t n = b.headers.size();
                for (const auto& r : b.rows) n = std::max(n, r.size());
                if (n == 0) break;
                const double size = 8.6, lh = size * 1.28, pad = 3.2;
                std::vector<double> widths = b.widths;
                if (widths.size() != n) {
                    widths.assign(n, 0.0);
                    for (std::size_t c = 0; c < n; ++c) {
                        double m = c < b.headers.size() ? widthOf(toWinAnsi(b.headers[c]), size, Font::Bold) : 20;
                        for (const auto& r : b.rows)
                            if (c < r.size()) m = std::max(m, std::min(kTextW * 0.5, widthOf(toWinAnsi(r[c]), size, Font::Regular)));
                        widths[c] = m + 2 * pad + 2;
                    }
                }
                double sum = 0;
                for (const double x : widths) sum += x;
                for (auto& x : widths) x = x / sum * kTextW;
                const bool header = std::any_of(b.headers.begin(), b.headers.end(), [](const std::string& h) { return !h.empty(); });
                const auto cells = [&](const std::vector<std::string>& row, bool head) {
                    std::vector<std::vector<std::string>> out(n);
                    for (std::size_t c = 0; c < n; ++c)
                        out[c] = wrap(toWinAnsi(c < row.size() ? row[c] : std::string{}), size, head || (c == 0 && !header) ? Font::Bold : Font::Regular,
                                      widths[c] - 2 * pad);
                    return out;
                };
                const auto heightOf = [&](const std::vector<std::vector<std::string>>& c) {
                    std::size_t m = 1;
                    for (const auto& x : c) m = std::max(m, x.size());
                    return static_cast<double>(m) * lh + 2 * pad;
                };
                const auto drawRow = [&](const std::vector<std::vector<std::string>>& c, bool head, bool zebra) {
                    const double h = heightOf(c);
                    const double top = pdf.y;
                    if (head) pdf.rect(kLeft, top - h, kTextW, h, 0.86, 0.9, 0.95);
                    else if (!header) pdf.rect(kLeft, top - h, widths[0], h, 0.95, 0.955, 0.97);
                    else if (zebra) pdf.rect(kLeft, top - h, kTextW, h, 0.975, 0.978, 0.985);
                    double x = kLeft;
                    for (std::size_t k = 0; k < n; ++k) {
                        double yy = top - pad;
                        for (const auto& l : c[k]) {
                            yy -= lh;
                            pdf.text(x + pad, yy + size * 0.3, l, size, head || (k == 0 && !header) ? Font::Bold : Font::Regular);
                        }
                        x += widths[k];
                    }
                    // Les filets : le bas de la ligne, les colonnes.
                    pdf.line(kLeft, top - h, kLeft + kTextW, top - h, 0.75, 0.4);
                    x = kLeft;
                    for (std::size_t k = 0; k <= n; ++k) {
                        pdf.line(x, top, x, top - h, 0.75, 0.4);
                        if (k < n) x += widths[k];
                    }
                    pdf.y -= h;
                };
                const auto head = header ? cells(b.headers, true) : std::vector<std::vector<std::string>>{};
                const double headH = header ? heightOf(head) : 0;
                const auto startTable = [&] {
                    pdf.line(kLeft, pdf.y, kLeft + kTextW, pdf.y, 0.75, 0.4);
                    if (header) drawRow(head, true, false);
                };
                bool first = true;
                std::size_t index = 0;
                for (const auto& r : b.rows) {
                    const auto c = cells(r, false);
                    const double h = heightOf(c);
                    if (first) {
                        pdf.ensure(headH + h + 2);            // l'en-tete ne reste pas seul en bas de page
                        startTable();
                        first = false;
                    } else if (pdf.y - h < kBottom) {
                        pdf.newPage();                         // l'en-tete se repete
                        startTable();
                    }
                    drawRow(c, false, index % 2 == 1);
                    ++index;
                }
                if (b.rows.empty() && header) {
                    pdf.ensure(headH + 2);
                    pdf.line(kLeft, pdf.y, kLeft + kTextW, pdf.y, 0.75, 0.4);
                    drawRow(head, true, false);
                }
                pdf.y -= 10;
                break;
            }
        }
    }
    // Le pied de chaque page : le titre, la page.
    const std::size_t total = pdf.pages.size();
    for (std::size_t i = 0; i < total; ++i) {
        std::string& s = pdf.pages[i];
        const std::string left = toWinAnsi(d.title);
        const std::string right = "Page " + std::to_string(i + 1) + " / " + std::to_string(total);
        s += "0.75 G 0.4 w " + pdfNumber(kLeft) + " 44 m " + pdfNumber(kPageW - kRight) + " 44 l S\n";
        s += "BT /F1 8 Tf 0.45 0.45 0.5 rg " + pdfNumber(kLeft) + " 32 Td " + pdfLiteral(left) + " Tj ET\n";
        s += "BT /F1 8 Tf 0.45 0.45 0.5 rg " + pdfNumber(kPageW - kRight - pdfTextWidth(right, 8, false)) + " 32 Td " + pdfLiteral(right) + " Tj ET\n";
    }

    // Les objets : 1 catalogue, 2 pages, 3-6 polices, les images, les signets, puis page et contenu.
    std::vector<std::string> objects;
    const std::size_t firstImage = 7;
    const std::size_t outlineRoot = firstImage + d.images.size();
    const std::size_t firstOutline = outlineRoot + 1;
    const std::size_t firstPage = firstOutline + pdf.outline.size();
    const auto ref = [](std::size_t obj) { return std::to_string(obj) + " 0 R"; };
    objects.push_back("<< /Type /Catalog /Pages 2 0 R" + std::string(pdf.outline.empty() ? "" : " /Outlines " + ref(outlineRoot) + " /PageMode /UseOutlines") + " >>");
    std::string kids;
    for (std::size_t p = 0; p < total; ++p) kids += ref(firstPage + 2 * p) + " ";
    objects.push_back("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(total) + " >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique /Encoding /WinAnsiEncoding >>");
    std::vector<std::string> binaries(objects.size());          // le contenu brut d'un objet (les images)
    for (const auto& img : d.images) {
        int w = img.width, h = img.height;
        if (w <= 0 || h <= 0) (void)jpegSize(img.jpeg, w, h);
        objects.push_back("<< /Type /XObject /Subtype /Image /Width " + std::to_string(std::max(1, w)) + " /Height " + std::to_string(std::max(1, h))
                          + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " + std::to_string(img.jpeg.size()) + " >>\nstream\n");
        binaries.emplace_back(img.jpeg.begin(), img.jpeg.end());
    }
    // Les signets : les titres 1, et leurs titres 2 dessous.
    {
        std::vector<std::size_t> tops;                    // les rangs (dans outline) des titres de niveau 0
        std::vector<std::vector<std::size_t>> children;
        for (std::size_t i = 0; i < pdf.outline.size(); ++i) {
            if (pdf.outline[i].level == 0 || tops.empty()) { tops.push_back(i); children.emplace_back(); }
            else children.back().push_back(i);
        }
        std::vector<std::string> items(pdf.outline.size());
        const auto dest = [&](const Outline& o) { return "[" + ref(firstPage + 2 * o.page) + " /XYZ 0 " + pdfNumber(std::min(kPageH, o.y + 6)) + " 0]"; };
        for (std::size_t t = 0; t < tops.size(); ++t) {
            const std::size_t i = tops[t];
            std::string o = "<< /Title " + pdfLiteral(toWinAnsi(pdf.outline[i].title)) + " /Parent " + ref(outlineRoot) + " /Dest " + dest(pdf.outline[i]);
            if (t > 0) o += " /Prev " + ref(firstOutline + tops[t - 1]);
            if (t + 1 < tops.size()) o += " /Next " + ref(firstOutline + tops[t + 1]);
            if (!children[t].empty())
                o += " /First " + ref(firstOutline + children[t].front()) + " /Last " + ref(firstOutline + children[t].back()) + " /Count -"
                   + std::to_string(children[t].size());
            items[i] = o + " >>";
            for (std::size_t c = 0; c < children[t].size(); ++c) {
                const std::size_t k = children[t][c];
                std::string oc = "<< /Title " + pdfLiteral(toWinAnsi(pdf.outline[k].title)) + " /Parent " + ref(firstOutline + i) + " /Dest " + dest(pdf.outline[k]);
                if (c > 0) oc += " /Prev " + ref(firstOutline + children[t][c - 1]);
                if (c + 1 < children[t].size()) oc += " /Next " + ref(firstOutline + children[t][c + 1]);
                items[k] = oc + " >>";
            }
        }
        std::string root = "<< /Type /Outlines";
        if (!tops.empty()) root += " /First " + ref(firstOutline + tops.front()) + " /Last " + ref(firstOutline + tops.back()) + " /Count " + std::to_string(tops.size());
        objects.push_back(root + " >>");
        binaries.emplace_back();
        for (auto& it : items) {
            objects.push_back(std::move(it));
            binaries.emplace_back();
        }
    }
    std::string fonts = "/Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >>";
    for (std::size_t p = 0; p < total; ++p) {
        std::string xobjects;
        for (const int im : pdf.pageImages[p]) xobjects += "/Im" + std::to_string(im) + " " + ref(firstImage + static_cast<std::size_t>(im)) + " ";
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pdfNumber(kPageW) + " " + pdfNumber(kPageH) + "] /Resources << " + fonts
                          + (xobjects.empty() ? std::string{} : " /XObject << " + xobjects + ">>") + " >> /Contents " + ref(firstPage + 2 * p + 1) + " >>");
        binaries.emplace_back();
        objects.push_back("<< /Length " + std::to_string(pdf.pages[p].size()) + " >>\nstream\n" + pdf.pages[p] + "endstream");
        binaries.emplace_back();
    }
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i];
        if (i < binaries.size() && !binaries[i].empty()) out += binaries[i] + "\nendstream";
        out += "\nendobj\n";
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info << /Title " + pdfLiteral(toWinAnsi(d.title))
         + " /Producer (XpgAnalyzer) >> >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(out.begin(), out.end());
}

} // namespace hmi
