#include "HmiPanes.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : XPGAnalyser IHM - pas de variables du programme
#include "../../hmi/HmiMigrate.hpp"   // 1.11.18 (lot 5) : des blocs VAR a migrer ?
#include "../../hmi/HmiDuplicate.hpp"   // 1.10.4 : "Remplacer..." ; 1.11 (REP) : le constat d'un repere qui n'est pas une variable
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiTemplates.hpp"

#include "HmiImages.hpp"
#include "HmiPaneKit.hpp"             // lot 13 : la couleur de la gravite

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiOperators.hpp"   // 1.10
#include "../../import/ProjectAnalyzer.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace app {

using hmi::Id;
using hmi::kNoId;
namespace PG = ui;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string bytesText(double b) {
    char buf[64];
    if (b < 0) return "?";
    // Un BOOL fait un bit : "0 o" laissait croire qu'il ne coute rien.
    if (b > 0 && b < 1) std::snprintf(buf, sizeof buf, "%.0f bit%s", b * 8, b * 8 > 1 ? "s" : "");
    else if (b < 1024) std::snprintf(buf, sizeof buf, "%.0f o", b);
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f Ko", b / 1024);
    else std::snprintf(buf, sizeof buf, "%.1f Mo", b / 1024 / 1024);
    return buf;
}

// La taille d'un type elementaire, ou d'un tableau de types elementaires,
// quand l'analyse de l'import n'est pas la (un projet rouvert depuis son
// dossier n'a pas de rapport d'analyse). -1 : inconnue.
double elementaryBytes(std::string_view type) {
    std::string t(type);
    for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (t.rfind("ARRAY[", 0) == 0) {
        const auto dots = t.find("..");
        const auto close = t.find(']');
        const auto of = t.find(" OF ");
        if (dots == std::string::npos || close == std::string::npos || of == std::string::npos) return -1;
        const long lo = std::atol(t.substr(6, dots - 6).c_str());
        const long hi = std::atol(t.substr(dots + 2, close - dots - 2).c_str());
        const double each = elementaryBytes(std::string_view(t).substr(of + 4));
        return each < 0 || hi < lo ? -1.0 : each * static_cast<double>(hi - lo + 1);
    }
    if (t == "BOOL") return 0.125;
    if (t == "EBOOL" || t == "BYTE" || t == "SINT" || t == "USINT") return 1;
    if (t == "INT" || t == "UINT" || t == "WORD") return 2;
    if (t == "DINT" || t == "UDINT" || t == "DWORD" || t == "REAL" || t == "TIME" || t == "DATE"
        || t == "TOD" || t == "TIME_OF_DAY")
        return 4;
    if (t == "LREAL" || t == "LINT" || t == "ULINT" || t == "LWORD" || t == "DT" || t == "DATE_AND_TIME") return 8;
    if (t == "STRING") return 17;                          // 16 caracteres + le zero
    if (t.rfind("STRING[", 0) == 0) return std::atof(t.c_str() + 7) + 1;
    return -1;
}

// Une table de chaines, ordonnee comme on la lui donne sauf tri demande.
class RowsModel final : public ui::ITableModel {
public:
    RowsModel(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows,
              std::vector<ui::CellStyle> firstColumnStyles = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(firstColumnStyles)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return headers_[c]; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return (c == 0 && r < styles_.size()) ? styles_[r] : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        return cellText(a, c) < cellText(b, c);
    }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle> styles_;
};

} // namespace

// ------------------------------------------------------------ utilisations -
HmiUsageIndex hmiUsageIndex(const hmi::Project& p) {
    HmiUsageIndex idx;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            for (const auto& prop : o.props) {
                std::vector<std::string> roots;
                if (!prop.expr.empty()) roots = hmi::scanRoots(prop.expr);
                if (prop.key == "text")
                    for (const auto& r : hmi::TextTemplate::compile(prop.value).roots()) roots.push_back(r);
                if (prop.key == "variable" && !prop.value.empty())
                    for (const auto& r : hmi::scanRoots(prop.value)) roots.push_back(r);
                std::sort(roots.begin(), roots.end());
                roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
                // En MAJUSCULES : l'ST ne distingue pas la casse, "armoires[0]"
                // dans une vue lit bien la variable declaree "Armoires".
                for (const auto& r : roots) idx[upper(r)].push_back(v.name + "/" + o.name + "." + prop.key);
            }
    // Le lot 4 lit aussi des variables : conditions et messages d'alarmes,
    // elements de recettes, variables archivees, autorisations.
    const auto cite = [&](const std::vector<std::string>& roots, const std::string& where) {
        std::vector<std::string> unique = roots;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        for (const auto& r : unique) idx[upper(r)].push_back(where);
    };
    for (const auto& a : p.alarms) {
        auto roots = hmi::scanRoots(a.condition);
        for (const auto& r : hmi::TextTemplate::compile(a.message).roots()) roots.push_back(r);
        cite(roots, "alarme " + a.name);
    }
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields)
            if (!f.variable.empty()) cite(hmi::scanRoots(f.variable), "recette " + r.name + "/" + f.name);
    for (const auto& e : p.history.archived) cite(hmi::scanRoots(e), "historique (archiv\xC3\xA9" "e)");
    for (const auto& u : p.security.users)
        if (!u.expression.empty()) cite(hmi::scanRoots(u.expression), "utilisateur " + u.login);
    return idx;
}

// --------------------------------------------------------------- variables -
HmiVariableTableModel::HmiVariableTableModel(std::shared_ptr<const domain::Project> plc,
                                             const importer::AnalysisReport* report, HmiUsageIndex usage,
                                             Filter filter) {
    if (!plc) return;
    std::map<std::string, double, std::less<>> sizes;
    if (report)
        for (const auto& t : report->typeUsage) sizes[t.name] = t.bytesEach();
    for (const auto& v : plc->variables) {
        using S = domain::VariableScope;
        if (v.scope == S::DerivedMember) continue;
        const bool global = v.scope == S::Global;
        const bool local = v.scope == S::Local || v.scope == S::Public || v.scope == S::Input
                        || v.scope == S::Output || v.scope == S::InOut;
        const bool ddt = v.type.klass == domain::TypeClass::Derived
                      || (v.type.klass == domain::TypeClass::Array && v.type.derivedIndex != domain::kNoIndex);
        const bool dfb = v.type.klass == domain::TypeClass::FunctionBlock;
        const bool io = v.located;
        switch (filter) {
            case Filter::All: break;
            case Filter::Global: if (!global) continue; break;
            case Filter::Local:  if (!local) continue; break;
            case Filter::Ddt:    if (!ddt) continue; break;
            case Filter::Dfb:    if (!dfb) continue; break;
            case Filter::Io:     if (!io) continue; break;
        }
        Row r;
        r.name = std::string(plc->strings.text(v.name));
        r.type = std::string(plc->strings.text(v.type.name));
        switch (v.scope) {
            case S::Global:   r.scope = "Globale"; break;
            case S::Local:
            case S::Public:   r.scope = "Locale"; break;
            case S::Input:    r.scope = "Entr\xC3\xA9" "e"; break;
            case S::Output:   r.scope = "Sortie"; break;
            case S::InOut:    r.scope = "Entr\xC3\xA9" "e-sortie"; break;
            case S::Constant: r.scope = "Constante"; break;
            default:          r.scope = "?"; break;
        }
        if (!global && v.owner != domain::kNoIndex && v.owner < plc->pous.size())
            r.scope += " \xC2\xB7 " + std::string(plc->strings.text(plc->pous[v.owner].name));
        // La reference : l'adresse d'une E/S ou d'un mot localise (%MW100,
        // %I0.2.3) - ce qu'on cherche dans une liste de variables d'IHM.
        r.reference = io ? v.address.raw : std::string{};
        r.io = io;
        if (const auto it = usage.find(upper(r.name)); it != usage.end() && global) {
            r.usage = it->second.size();
            r.where = it->second.front() + (it->second.size() > 1 ? "  (+" + std::to_string(it->second.size() - 1) + ")" : "");
        }
        // La taille vient du modele du projet (types derives et tableaux de
        // types derives compris) : elle ne depend pas du rapport d'analyse, qui
        // manque quand le projet est rouvert depuis son dossier.
        if (const auto bits = domain::typeSizeInBits(*plc, v.type); bits > 0) r.bytes = bits / 8.0;
        else if (const auto it = sizes.find(r.type); it != sizes.end() && it->second > 0) r.bytes = it->second;
        else r.bytes = elementaryBytes(r.type);
        rows_.push_back(std::move(r));
    }
}

std::string HmiVariableTableModel::headerText(std::size_t c) const {
    static const char* h[] = {"Nom", "Type", "Port\xC3\xA9" "e", "Utilis\xC3\xA9" "e par l'IHM", "R\xC3\xA9" "f\xC3\xA9rence",
                              "Taille m\xC3\xA9moire"};
    return c < ColumnCount ? h[c] : "";
}

std::string HmiVariableTableModel::cellText(ui::RowIndex r, std::size_t c) const {
    if (r >= rows_.size()) return {};
    const auto& row = rows_[r];
    switch (c) {
        case Name: return row.name;
        case Type: return row.type;
        case Scope: return row.scope;
        case Usage: return row.usage ? std::to_string(row.usage) + "  \xC2\xB7  " + row.where : std::string("-");
        case Reference: return row.reference.empty() ? std::string("-") : row.reference;
        case Size: return bytesText(row.bytes);
        default: return {};
    }
}

ui::CellStyle HmiVariableTableModel::cellStyle(ui::RowIndex r, std::size_t c) const {
    ui::CellStyle s;
    if (r >= rows_.size()) return s;
    if (c == Name) s.icon = rows_[r].io ? ui::Icon::LocatedVariable : ui::Icon::Variable;
    if (c == Usage && rows_[r].usage) s.bold = true;
    return s;
}

bool HmiVariableTableModel::less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const {
    const auto& x = rows_[a];
    const auto& y = rows_[b];
    if (c == Usage) return x.usage < y.usage;
    if (c == Size) return x.bytes < y.bytes;
    return lower(cellText(a, c)) < lower(cellText(b, c));
}

// ----------------------------------------------------------- Configuration -
HmiConfigPane::HmiConfigPane(std::string id, hmi::DocumentPtr doc, Apply apply,
                             std::shared_ptr<const domain::Project> plc, const importer::AnalysisReport* report,
                             std::function<std::uint64_t()> bytesOnDisk)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), plc_(std::move(plc)),
      report_(report), bytesOnDisk_(std::move(bytesOnDisk)) {
    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, this->id() + ".split");
    auto top = std::make_unique<HmiTitledPanel>(this->id() + ".configPanel", "CONFIGURATION DU PROJET IHM");
    grid_ = &static_cast<ui::PropertyGrid&>(top->setBody(std::make_unique<ui::PropertyGrid>(this->id() + ".grid")));
    split->addPane(std::move(top), 0.52f, 160.f);

    // 1.11.1 (API-V, decision 104) : les variables de l'automate EN ARBRE (les
    // Globales, puis les unites et leurs portees ; les instances et les
    // tableaux se deplient), avec son filtre et sa recherche - plus la liste plate.
    auto vars = std::make_unique<HmiTitledPanel>(this->id() + ".varsPanel", "VARIABLES DU PROGRAMME");
    apiVars_ = &static_cast<HmiApiVarsView&>(vars->setBody(std::make_unique<HmiApiVarsView>(this->id() + ".vars")));
    if (core::hasApi()) {
        varsPanel_ = &static_cast<HmiTitledPanel&>(split->addPane(std::move(vars), 0.48f, 140.f));
    } else {
        // 1.12.0 : XPGAnalyser IHM n'a pas d'automate - le panneau existe (ses liens), hors de l'ecran.
        varsPanel_ = vars.get();
        unshownVars_ = std::move(vars);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));

    // Le double-clic sur une variable : son nom complet (API.…) dans le script
    // montre a cote (un onglet de scripts de l'IHM a l'ecran), sinon dans le
    // presse-papiers.
    links_ += apiVars_->insertName->connect([this](const std::string& path) {
        ui::Widget* root = this;
        while (root->parent()) root = root->parent();
        if (HmiApiVarsView::insertInShownScript(*root, path)) {
            message_ = path + " ins\xC3\xA9r\xC3\xA9 dans le script";
            return;
        }
        ui::setClipboardText(path);
        message_ = path + " copi\xC3\xA9 : colle-le dans un script (Ctrl+V), ou glisse la variable dans le script";
    });
    // Un emploi choisi (« Utilisee par l'IHM ») : l'ecran y mene.
    links_ += apiVars_->useChosen->connect([this](const std::string& path, std::size_t index) {
        const auto issue = issueOfUse(path, index);
        if (!issue.category.empty()) useActivated->emit(issue);
    });
    links_ += doc_->changed->connect([this](Id) { dirty_ = true; invalidateLayout(); });
}

void HmiConfigPane::setPlc(std::shared_ptr<const domain::Project> plc, const importer::AnalysisReport* report) {
    plc_ = std::move(plc);
    report_ = report;
    dirty_ = true;
    invalidateLayout();
}

void HmiConfigPane::refresh() {
    dirty_ = true;
    invalidateLayout();
}

void HmiConfigPane::ensureBuilt() {
    if (dirty_) rebuild();
}

void HmiConfigPane::onLayout() {
    split_->setBounds(bounds());
    if (dirty_) rebuild();
}

void HmiConfigPane::rebuild() {
    dirty_ = false;
    const auto& p = doc_->project;
    const auto& cfg = p.config;
    using P = ui::PropertyGrid;
    auto commitCfg = [this](std::string label, std::function<void(hmi::Project&, const std::string&)> fn) {
        return [this, label, fn](std::string_view s) {
            const std::string value(s);
            auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& pp) { fn(pp, value); });
            if (cmd) apply_(std::move(cmd));
            return true;
        };
    };
    auto prop = [](std::string name, std::string value, P::ValueType t, std::function<bool(std::string_view)> commit = {},
                   std::string help = {}, std::vector<std::string> choices = {}) {
        P::Property pr;
        pr.name = std::move(name);
        pr.value = std::move(value);
        pr.type = commit ? t : P::ValueType::ReadOnly;
        pr.commit = std::move(commit);
        pr.description = std::move(help);
        pr.enumValues = std::move(choices);
        return pr;
    };
    std::vector<P::Category> cats;
    P::Category ident;
    ident.name = "Projet IHM";
    ident.properties.push_back(prop("Nom", cfg.name, P::ValueType::Text,
                                    commitCfg("Nom du projet IHM", [](hmi::Project& pp, const std::string& v) { pp.config.name = v; })));
    ident.properties.push_back(prop("Description", cfg.description, P::ValueType::Text,
                                    commitCfg("Description", [](hmi::Project& pp, const std::string& v) { pp.config.description = v; })));
    ident.properties.push_back(prop("Version", cfg.version, P::ValueType::Text,
                                    commitCfg("Version", [](hmi::Project& pp, const std::string& v) { pp.config.version = v; })));
    ident.properties.push_back(prop("Auteur", cfg.author, P::ValueType::Text,
                                    commitCfg("Auteur", [](hmi::Project& pp, const std::string& v) { pp.config.author = v; })));
    ident.properties.push_back(prop("Date de cr\xC3\xA9" "ation", cfg.created.empty() ? "(\xC3\xA0 l'enregistrement)" : cfg.created,
                                    P::ValueType::ReadOnly));
    ident.properties.push_back(prop("Date de modification", cfg.modified.empty() ? "(\xC3\xA0 l'enregistrement)" : cfg.modified,
                                    P::ValueType::ReadOnly));
    cats.push_back(std::move(ident));

    P::Category res;
    res.name = "R\xC3\xA9solution";
    auto setInt = [](int& field, const std::string& v, int lo) {
        double n = 0;
        if (hmi::parseNumber(v, n) && n >= lo) field = static_cast<int>(n);
    };
    res.properties.push_back(prop("Largeur", std::to_string(cfg.width), P::ValueType::Integer,
                                  commitCfg("Largeur", [setInt](hmi::Project& pp, const std::string& v) { setInt(pp.config.width, v, 64); }),
                                  "La taille des NOUVELLES vues. Une vue existante garde la sienne (propri\xC3\xA9t\xC3\xA9s de la vue)."));
    res.properties.push_back(prop("Hauteur", std::to_string(cfg.height), P::ValueType::Integer,
                                  commitCfg("Hauteur", [setInt](hmi::Project& pp, const std::string& v) { setInt(pp.config.height, v, 64); })));
    res.properties.push_back(prop("Orientation", cfg.orientation, P::ValueType::Enum,
                                  commitCfg("Orientation", [](hmi::Project& pp, const std::string& v) {
                                      pp.config.orientation = v;
                                      const bool portrait = v == "Portrait";
                                      if (portrait != (pp.config.height > pp.config.width)) std::swap(pp.config.width, pp.config.height);
                                  }), "", {"Paysage", "Portrait"}));
    std::vector<std::string> names;
    std::string start;
    for (const auto& v : p.views) {
        names.push_back(v.name);
        if (v.id == cfg.startView) start = v.name;
    }
    res.properties.push_back(prop("Vue de d\xC3\xA9marrage", start, P::ValueType::Enum,
                                  commitCfg("Vue de d\xC3\xA9marrage", [](hmi::Project& pp, const std::string& v) {
                                      if (const auto* vv = pp.viewByName(v)) pp.config.startView = vv->id;
                                  }), "La vue ouverte au lancement de la simulation.", names));
    cats.push_back(std::move(res));

    P::Category run;
    run.name = "Ex\xC3\xA9" "cution";
    run.properties.push_back(prop("Cycle IHM (ms)", std::to_string(cfg.cycleMs), P::ValueType::Integer,
                                  commitCfg("Cycle IHM", [setInt](hmi::Project& pp, const std::string& v) { setInt(pp.config.cycleMs, v, 10); }),
                                  "La cadence de l'IHM en simulation : scripts OnCycle, fronts, conditions surveill\xC3\xA9" "es. "
                                  "10 ms au moins ; 100 ms suffisent \xC3\xA0 un op\xC3\xA9rateur."));
    // Lot 12 : un glisser horizontal sur le fond d'une vue change de vue (ecran tactile).
    run.properties.push_back(prop("Changer de vue en glissant", cfg.swipeNavigation ? "TRUE" : "FALSE", P::ValueType::Boolean,
                                  commitCfg("Changer de vue en glissant", [](hmi::Project& pp, const std::string& v) {
                                      pp.config.swipeNavigation = hmi::parseBool(v, false);
                                  }),
                                  "\xC3\x89" "cran tactile : un glisser horizontal ample sur le fond d'une vue passe \xC3\xA0 la vue suivante "
                                  "(vers la gauche) ou pr\xC3\xA9" "c\xC3\xA9" "dente, dans l'ordre de la barre de navigation de la vue, "
                                  "sinon de l'arbre."));
    cats.push_back(std::move(run));

    // Lot 13 : Generer plus exigeant - la case, et les seuils (0 : pas de controle).
    P::Category quality;
    quality.name = "Qualit\xC3\xA9 (G\xC3\xA9n\xC3\xA9rer)";
    quality.properties.push_back(prop("Contr\xC3\xB4les de qualit\xC3\xA9", cfg.quality ? "TRUE" : "FALSE", P::ValueType::Boolean,
                                      commitCfg("Contr\xC3\xB4les de qualit\xC3\xA9", [](hmi::Project& pp, const std::string& v) {
                                          pp.config.quality = hmi::parseBool(v, true);
                                      }),
                                      "G\xC3\xA9n\xC3\xA9rer dit aussi ce qui tourne mais sert mal : objets hors de la vue ou toujours "
                                      "invisibles, textes qui d\xC3\xA9" "bordent, contraste faible, vues jamais atteintes, cibles trop "
                                      "petites pour un doigt, vues trop charg\xC3\xA9" "es."));
    quality.properties.push_back(prop("Cible tactile minimale (px)", std::to_string(cfg.touchMin), P::ValueType::Integer,
                                      commitCfg("Cible tactile minimale", [setInt](hmi::Project& pp, const std::string& v) { setInt(pp.config.touchMin, v, 0); }),
                                      "Le plus petit c\xC3\xB4t\xC3\xA9 d'un bouton, d'une case, d'un choix. 32 px valent environ 8 mm "
                                      "sur un \xC3\xA9" "cran de 21 pouces en 1920 \xC3\x97 1080 ; 0 : pas de contr\xC3\xB4le."));
    quality.properties.push_back(prop("Contraste minimal (x:1)", hmi::formatNumber(cfg.contrastMin), P::ValueType::Text,
                                      commitCfg("Contraste minimal", [](hmi::Project& pp, const std::string& v) {
                                          double n = 0;
                                          if (hmi::parseNumber(v, n) && n >= 0 && n <= 21) pp.config.contrastMin = n;
                                      }),
                                      "Le rapport entre la clart\xC3\xA9 du texte et celle de son fond (WCAG) : 4,5 pour un texte courant, "
                                      "3 pour un grand texte (24 px et plus) ; 0 : pas de contr\xC3\xB4le."));
    quality.properties.push_back(prop("Vue charg\xC3\xA9" "e au-del\xC3\xA0 de (objets)", std::to_string(cfg.heavyObjects), P::ValueType::Integer,
                                      commitCfg("Vue charg\xC3\xA9" "e", [setInt](hmi::Project& pp, const std::string& v) { setInt(pp.config.heavyObjects, v, 0); }),
                                      "Au-del\xC3\xA0, G\xC3\xA9n\xC3\xA9rer pr\xC3\xA9vient : le dessin et le cycle ralentissent. "
                                      "0 : pas de contr\xC3\xB4le."));
    cats.push_back(std::move(quality));

    // Lot 13 : l'affichage au lancement de l'IHM (HmiDisplay) - l'operateur le
    // change en marche (menu Parametres systeme, SYS.TextScale...).
    P::Category shown;
    shown.name = "Affichage (au lancement)";
    shown.properties.push_back(prop("Taille du texte (%)", std::to_string(cfg.textScale), P::ValueType::Enum,
                                    commitCfg("Taille du texte", [](hmi::Project& pp, const std::string& v) {
                                        double n = 0;
                                        if (hmi::parseNumber(v, n) && n >= 50 && n <= 300) pp.config.textScale = static_cast<int>(n);
                                    }),
                                    "Tous les textes de l'IHM en marche grandissent (100 : ceux de la conception). L'op\xC3\xA9rateur la "
                                    "change aussi dans le menu Param\xC3\xA8tres syst\xC3\xA8me (SYS.TextScale).",
                                    {"100", "125", "150", "175"}));
    shown.properties.push_back(prop("Couleurs", cfg.colorMode, P::ValueType::Enum,
                                    commitCfg("Couleurs", [](hmi::Project& pp, const std::string& v) {
                                        pp.config.colorMode = v == "daltonien" ? "daltonien" : "normal";
                                    }),
                                    "Daltonien : le vert passe au bleu, le rouge au vermillon, l'orange \xC3\xA0 l'ambre - des couleurs "
                                    "qu'on distingue sans voir le vert et le rouge (SYS.ColorMode).",
                                    {"normal", "daltonien"}));
    shown.properties.push_back(prop("Symboles sur les voyants", cfg.statusSymbols ? "TRUE" : "FALSE", P::ValueType::Boolean,
                                    commitCfg("Symboles sur les voyants", [](hmi::Project& pp, const std::string& v) {
                                        pp.config.statusSymbols = hmi::parseBool(v, false);
                                    }),
                                    "Une coche, une croix, un point d'exclamation ou un tiret dans chaque voyant, un triangle sur un "
                                    "symbole en d\xC3\xA9" "faut : l'\xC3\xA9tat se lit sans la couleur (SYS.StatusSymbols)."));
    shown.properties.push_back(prop("Th\xC3\xA8me", cfg.theme, P::ValueType::Enum,
                                    commitCfg("Th\xC3\xA8me", [](hmi::Project& pp, const std::string& v) {
                                        pp.config.theme = v == "jour" ? "jour" : "nuit";
                                    }),
                                    "Nuit : les couleurs de la conception. Jour : les fonds sombres deviennent clairs et les textes "
                                    "sombres, les couleurs franches restent - pour un \xC3\xA9" "cran en plein jour (SYS.Theme, le S\xC3\xA9lecteur de th\xC3\xA8me).",
                                    {"nuit", "jour"}));
    cats.push_back(std::move(shown));

    const auto st = p.statistics();
    P::Category stats;
    stats.name = "Statistiques";
    stats.properties.push_back(prop("Nombre de vues", std::to_string(st.views), P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Nombre d'objets", std::to_string(st.objects) + (st.groups == 0 ? std::string("  (aucun groupe)")
                                      : "  (dont " + std::to_string(st.groups) + (st.groups > 1 ? " groupes)" : " groupe)")),
                                    P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Calques", std::to_string(st.layers), P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Propri\xC3\xA9t\xC3\xA9s anim\xC3\xA9" "es (expressions)", std::to_string(st.expressions), P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Nombre de scripts", std::to_string(st.scripts) + "  (dont "
                                      + std::to_string(p.programs.scripts.size()) + " g\xC3\xA9n\xC3\xA9raux)", P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Nombre d'actions", std::to_string(st.actions), P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Variables IHM", std::to_string(p.programs.variables.size()), P::ValueType::ReadOnly));
    {
        std::size_t withReturn = 0;
        for (const auto& f : p.programs.functions) withReturn += !f.returnType.empty();
        stats.properties.push_back(prop("Fonctions IHM", std::to_string(p.programs.functions.size()) + "  (dont "
                                            + std::to_string(withReturn) + " avec retour)",
                                        P::ValueType::ReadOnly));
    }
    stats.properties.push_back(prop("Nombre d'alarmes", std::to_string(st.alarms), P::ValueType::ReadOnly));
    {
        std::size_t sets = 0;
        for (const auto& r : p.recipes) sets += r.records.size();
        stats.properties.push_back(prop("Recettes", std::to_string(st.recipes) + "  (" + std::to_string(sets) + " jeu(x) de valeurs)",
                                        P::ValueType::ReadOnly));
        stats.properties.push_back(prop("Utilisateurs", std::to_string(st.users) + "  (s\xC3\xA9" "curit\xC3\xA9 "
                                            + std::string(p.security.enabled ? "active" : "inactive") + ")",
                                        P::ValueType::ReadOnly));
    }
    stats.properties.push_back(prop("Nombre de ressources", std::to_string(st.resources), P::ValueType::ReadOnly));
    stats.properties.push_back(prop("Taille m\xC3\xA9moire (estim\xC3\xA9" "e)", bytesText(static_cast<double>(st.bytesInMemory)),
                                    P::ValueType::ReadOnly));
    const auto disk = bytesOnDisk_ ? bytesOnDisk_() : 0;
    stats.properties.push_back(prop("Poids du projet (disque)", disk ? bytesText(static_cast<double>(disk)) : "non enregistr\xC3\xA9",
                                    P::ValueType::ReadOnly));
    cats.push_back(std::move(stats));

    // Les compteurs de variables, lus dans le programme a chaque fois.
    if (plc_ && core::hasApi()) {
        std::size_t g = 0, l = 0, d = 0, f = 0, io = 0;
        for (const auto& v : plc_->variables) {
            using S = domain::VariableScope;
            if (v.scope == S::DerivedMember) continue;
            if (v.scope == S::Global) ++g; else ++l;
            if (v.type.klass == domain::TypeClass::Derived) ++d;
            if (v.type.klass == domain::TypeClass::FunctionBlock) ++f;
            if (v.located) ++io;
        }
        P::Category vars;
        vars.name = "Variables du programme (synchronis\xC3\xA9" "es)";
        vars.properties.push_back(prop("Globales", std::to_string(g), P::ValueType::ReadOnly));
        vars.properties.push_back(prop("Locales et param\xC3\xA8tres", std::to_string(l), P::ValueType::ReadOnly));
        vars.properties.push_back(prop("Instances de DDT", std::to_string(d), P::ValueType::ReadOnly));
        vars.properties.push_back(prop("Instances de DFB", std::to_string(f), P::ValueType::ReadOnly));
        vars.properties.push_back(prop("E/S localis\xC3\xA9" "es", std::to_string(io), P::ValueType::ReadOnly));
        cats.push_back(std::move(vars));
    }
    grid_->setCategories(std::move(cats));

    // 1.11.1 (API-V) : l'arbre, sur le modele d'API-M (refait a chaque fois : le
    // programme, la table des adresses et les emplois ont pu changer ; ce qui
    // est deplie reste, par la cle des noeuds).
    auto model = std::make_shared<const hmi::apivars::Model>(hmi::apivars::Model::build(plc_, &p));
    apiModel_ = model;
    apiNodes_.clear();
    std::vector<ApiVarNode> roots;
    std::size_t total = 0;
    for (const auto& n : model->roots()) {
        if (n.kind == hmi::apivars::NodeKind::Group && n.group == hmi::apivars::GroupKind::Unit) {
            for (const auto& scope : model->children(n)) total += scope.count;
        } else if (n.kind == hmi::apivars::NodeKind::Group) {
            total += n.count;
        }
        roots.push_back(adoptApiNode(n));
    }
    apiVars_->setFetch([this, model](const ApiVarNode& v) {
        std::vector<ApiVarNode> out;
        const auto it = apiNodes_.find(v.key);
        if (it == apiNodes_.end()) return out;
        const hmi::apivars::Node parent = it->second;     // copie : adoptApiNode remplit la table
        for (const auto& c : model->children(parent)) out.push_back(adoptApiNode(c));
        return out;
    });
    apiVars_->setSearchSource([model](std::string_view text) { return model->search(text); });
    // Accessibles en ecriture, sous ce qui n'est pas deplie : les chemins de la
    // table des adresses (Configuration > Communication) que le modele dit
    // ecrivables - un membre, une variable d'unite n'ont d'adresse que par elle.
    apiVars_->setFilterSource([model, rows = p.comm.addresses](HmiApiVarsView::Filter f) {
        std::vector<std::string> out;
        if (f != HmiApiVarsView::Filter::Writable) return out;
        for (const auto& a : rows) {
            hmi::apivars::Node n;
            if (!a.variable.empty() && model->node(a.variable, n) && n.writable()) out.push_back(n.path);
        }
        return out;
    });
    apiVars_->setNodes(std::move(roots));
    varsPanel_->setTitle("VARIABLES DU PROGRAMME  \xC2\xB7  " + std::to_string(total));
}

// Un emploi du modele, en constat (hmi::Issue) que l'ecran sait ouvrir : le
// script a sa ligne, la fonction, l'alarme, la recette, l'utilisateur,
// l'historique, la vue et son objet.
hmi::Issue HmiConfigPane::issueOfUse(const std::string& path, std::size_t index) const {
    hmi::Issue i;
    if (!apiModel_) return i;
    const auto uses = apiModel_->usesOf(path);
    if (index >= uses.size()) return i;
    const auto& u = uses[index];
    const auto starts = [&](std::string_view prefix) { return u.where.rfind(prefix, 0) == 0; };
    i.severity = hmi::Issue::Severity::Info;
    i.message = path + " : " + u.where;
    i.view = u.view;
    i.object = u.object;
    i.property = u.property;
    i.line = u.line;
    if (starts("fonction ")) {
        i.category = "Fonction";
        i.item = u.script;
    } else if (u.script != hmi::kNoId) {
        i.category = "Script";
        i.script = u.script;
    } else if (starts("alarme ")) {
        i.category = "Alarme";
        i.item = u.item;
    } else if (starts("recette ")) {
        i.category = "Recette";
        i.item = u.item;
    } else if (starts("utilisateur ")) {
        i.category = "Utilisateur";
        i.item = u.item;
    } else if (starts("historique")) {
        i.category = "Historique";
    } else {
        i.category = u.object != hmi::kNoId ? "Objet" : "Vue";
    }
    return i;
}

ApiVarNode HmiConfigPane::adoptApiNode(const hmi::apivars::Node& n) {
    apiNodes_[n.key] = n;
    auto v = apiVarNodeOf(n);
    // Utilisee par l'IHM : ou (la colonne en montre le premier, l'infobulle la
    // liste) - seulement pour ce qui est employe.
    if (n.uses > 0 && n.real() && apiModel_)
        for (const auto& u : apiModel_->usesOf(n.path)) v.uses.push_back(u.where);
    return v;
}

// ------------------------------------------------------------------- Vues --
namespace {
enum ViewAction : int { VNew = 1, VDuplicate, VDelete, VUp, VDown, VStart, VOpen, VDupReplace, VFromType,
                        VSaveTemplate, VExport, VImport,     // lot 20 : modeles, exporter, importer
                        VFolder,                             // lot 21 : un dossier
                        VExportSymbols, VImportSymbols };    // 1.11.2 (decision 162) : dans le dossier Symboles

// Lot 12 : LA VIGNETTE de la vue survolee dans la liste (sinon de la vue
// choisie) : son fond, ses objets, ce qu'elle emprunte a son ecran modele.
class ViewThumbnail final : public ui::Widget {
public:
    ViewThumbnail(std::string id, hmi::DocumentPtr doc, std::function<Id()> which)
        : ui::Widget(std::move(id)), doc_(std::move(doc)), which_(std::move(which)) {}
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, 1, b.h}, c.border);
        const Id id = which_ ? which_() : kNoId;
        const auto* v = id != kNoId ? doc_->project.view(id) : nullptr;
        const auto f = ctx.theme.font.smallUi;
        if (!v) {
            ctx.r.drawText({b.x + 12, b.y + 12}, "Survole une vue : son aper\xC3\xA7u.", f, c.textMuted);
            return;
        }
        ctx.r.drawText({b.x + 12, b.y + 10}, "Aper\xC3\xA7u : " + v->name, ctx.theme.font.ui, c.text);
        ctx.r.drawText({b.x + 12, b.y + 32},
                       std::to_string(v->width) + " x " + std::to_string(v->height) + "  \xC2\xB7  " + std::to_string(v->objects.size())
                           + " objet(s)  \xC2\xB7  " + std::string(hmi::viewRoleLabel(v->role)),
                       f, c.textMuted);
        // En haut du volet, a la largeur : pas une vignette flottant au milieu.
        const float w = b.w - 24;
        const float h = v->width > 0 ? std::min(std::max(0.f, b.h - 68), w * static_cast<float>(v->height) / static_cast<float>(v->width))
                                     : std::max(0.f, b.h - 68);
        paintHmiViewPreview(ctx.r, doc_->project, *v, {b.x + 12, b.y + 56, w, h}, ctx.theme);
    }
private:
    hmi::DocumentPtr    doc_;
    std::function<Id()> which_;
};
}

HmiViewsPane::HmiViewsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    auto tools = std::make_unique<HmiToolStrip>(this->id() + ".tools");
    tools->add(VNew, HmiGlyph::Plus, "Nouvelle vue, \xC3\xA0 la r\xC3\xA9solution du projet", "Nouvelle vue");
    tools->add(VDuplicate, HmiGlyph::Duplicate, "Dupliquer la vue (objets, calques, scripts)", "Dupliquer");
    tools->add(VDelete, HmiGlyph::Delete, "Supprimer la vue, ou le dossier choisi (ses vues remontent d'un cran) - Ctrl+Z les rend", "Supprimer");
    // Lot 21 : un dossier dans la liste (glisser une ou plusieurs lignes dessus pour les y ranger).
    tools->add(VFolder, HmiGlyph::Plus, "Nouveau dossier dans la liste, dans le dossier choisi (sans effet sur les noms) ; "
                                        "glisse des lignes dessus pour les y ranger, entre deux lignes pour les reordonner", "Dossier");
    tools->separator();
    tools->add(VUp, HmiGlyph::Up, "Monter dans la liste");
    tools->add(VDown, HmiGlyph::Down, "Descendre dans la liste");
    tools->add(VStart, HmiGlyph::StarFilled, "En faire la vue de d\xC3\xA9marrage", "D\xC3\xA9marrage");
    tools->add(VOpen, HmiGlyph::View, "Ouvrir dans l'\xC3\xA9" "diteur (ou double-clic)", "Ouvrir");
    tools->separator();
    tools->add(VDupReplace, HmiGlyph::Duplicate,
               "Dupliquer en rempla\xC3\xA7" "ant : une copie de la vue o\xC3\xB9 un texte devient un autre partout "
               "(Armoires[0] \xE2\x86\x92 Armoires[1], Vue_A \xE2\x86\x92 Vue_B)",
               "Dupliquer en rempla\xC3\xA7" "ant");
    tools->add(VFromType, HmiGlyph::Template,
               "G\xC3\xA9n\xC3\xA9rer depuis un type : une popup (ou une vue) faite des membres d'un DDT ou d'un DFB, "
               "lus \xC3\xA0 travers un param\xC3\xA8tre - une popup pour toutes les instances",
               "Depuis un type");
    // Lot 20 : les modeles et les paquets de vues.
    tools->separator();
    tools->add(VSaveTemplate, HmiGlyph::StarFilled,
               "Enregistrer la vue choisie comme mod\xC3\xA8le : dans ma biblioth\xC3\xA8que (tous mes projets) ou dans ce projet",
               "Enregistrer comme mod\xC3\xA8le\xE2\x80\xA6");
    tools->add(VExport, HmiGlyph::Export,
               "Exporter des vues (.xpgvues) avec ce dont elles ont besoin : symboles, popups, images, styles, variables",
               "Exporter les vues\xE2\x80\xA6");
    tools->add(VImport, HmiGlyph::Import, "Importer des vues d'un autre projet : comparer avant d'agir, un seul Ctrl+Z",
               "Importer des vues\xE2\x80\xA6");
    // 1.11.2 (decision 162) : dans le dossier Symboles, a la place des deux precedents.
    tools->add(VExportSymbols, HmiGlyph::Export,
               "Exporter des symboles (.xpgsymboles) avec ce dont ils ont besoin : symboles imbriqu\xC3\xA9s, images, styles, "
               "variables et types IHM",
               "Exporter les symboles\xE2\x80\xA6");
    tools->add(VImportSymbols, HmiGlyph::Import,
               "Importer des symboles (ou tout fichier fait par Exporter : vues, types IHM, fonctions, scripts) d'un autre projet : ce "
               "qu'ils emportent, renommer ou remplacer chaque nom en conflit, un seul Ctrl+Z",
               "Importer\xE2\x80\xA6");   // 1.11.2 (decision 174) : tout paquet
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // 1.11.2 (decision 162) : le dossier Symboles (6) montre ses deux boutons a
    // la place de ceux des vues (sans hote, ni les uns ni les autres ne servent).
    tools_->setVisibleWhen(VExport, [this] { return folder_ != 6; });
    tools_->setVisibleWhen(VImport, [this] { return folder_ != 6; });
    tools_->setVisibleWhen(VExportSymbols, [this] { return folder_ == 6 && static_cast<bool>(packageHosts_.exportSymbols); });
    tools_->setVisibleWhen(VImportSymbols, [this] { return folder_ == 6 && static_cast<bool>(packageHosts_.importSymbols); });
    auto table = std::make_unique<ui::TableView>(this->id() + ".table");
    table->setColumns({{"Vue", 260.f}, {"R\xC3\xB4le", 150.f}, {"Identifiant", 115.f, 60.f, true, true, true, ui::Align::End},
                       {"Taille", 120.f}, {"Objets", 90.f, 50.f, true, true, true, ui::Align::End},
                       {"Calques", 100.f, 50.f, true, true, true, ui::Align::End}, {"Param\xC3\xA8tres", 160.f},
                       {"Description", 320.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(this->id() + ".status")));
    // Lot 21 : la liste rangee en dossiers (plusieurs lignes choisies, glissees).
    folders_ = std::make_unique<HmiFolderTable>(*table_, doc_, apply_);
    links_ += folders_->message->connect([this](const std::string& text) { status_->setTransientMessage(text, 8.0); });
    links_ += folders_->relayout->connect([this] { refresh(); });
    // Lot 12 : la vignette de la vue survolee (sinon choisie).
    thumb_ = &addChild(std::make_unique<ViewThumbnail>(this->id() + ".thumb", doc_, [this]() -> Id {
        const int hover = table_->hoveredRow();
        if (hover >= 0 && static_cast<std::size_t>(hover) < table_->visibleRowCount()) {
            const Id row = folders_->itemAt(table_->viewRow(static_cast<std::size_t>(hover)));
            if (row != kNoId) return row;
        }
        return selectedView();
    }));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedView();
        auto push = [this](core::CommandPtr c) { if (c) apply_(std::move(c)); };
        switch (a) {
            case VFromType:
                if (onFromType_) onFromType_();
                break;
            case VSaveTemplate:
                if (packageHosts_.saveTemplate) packageHosts_.saveTemplate(sel);
                break;
            case VExport:
                if (packageHosts_.exportViews) packageHosts_.exportViews(sel);
                break;
            case VImport:
                if (packageHosts_.importViews) packageHosts_.importViews();
                break;
            case VExportSymbols:
                if (packageHosts_.exportSymbols) packageHosts_.exportSymbols(sel);
                break;
            case VImportSymbols:
                if (packageHosts_.importSymbols) packageHosts_.importSymbols();
                break;
            case VDupReplace: {
                if (sel == kNoId) break;
                if (onDuplicateReplace_) { onDuplicateReplace_(sel); break; }
                Id made = kNoId;
                push(hmi::changeProject(doc_, "Dupliquer la vue", [&](hmi::Project& p) {
                    const auto* src = p.view(sel);
                    if (src) made = hmi::design::duplicateViewReplacing(p, sel, src->name + "_copie", {}, {});
                }));
                refresh();
                selectView(made);
                break;
            }
            case VNew: {
                const std::string role = folderRole();
                if (onNew_) { onNew_(role); break; }
                Id made = kNoId;
                // Les memes tailles que le dialogue de l'ecran : une popup de 640 x 400
                // au plus, un bandeau de 80 pixels de haut.
                const auto& cfg = doc_->project.config;
                int w = cfg.width, h = cfg.height;
                std::string base = "Vue";
                if (role == "popup") { w = std::min(640, cfg.width); h = std::min(400, cfg.height); base = "Popup"; }
                else if (role == "entete") { h = 80; base = "Entete"; }
                else if (role == "pied") { h = 80; base = "Pied"; }
                else if (role == "modele") base = "Modele";
                else if (role == "symbole") { w = 240; h = 160; base = "Symbole"; }   // lot 10
                push(hmiNewViewCommand(doc_, hmi::uniqueViewName(doc_->project, base), w, h, {}, &made, role));
                refresh();
                selectView(made);
                break;
            }
            case VDuplicate: {
                if (sel == kNoId) break;
                Id made = kNoId;
                push(hmi::changeProject(doc_, "Dupliquer la vue", [&](hmi::Project& p) {
                    const auto* src = p.view(sel);
                    if (!src) return;
                    hmi::View copy = *src;
                    copy.id = p.allocate();
                    copy.name = hmi::uniqueViewName(p, src->name + "_copie");
                    std::map<Id, Id> ids;
                    for (auto& l : copy.layers) { const Id n = p.allocate(); ids[l.id] = n; l.id = n; }
                    for (auto& o : copy.objects) ids[o.id] = p.allocate();
                    for (auto& o : copy.objects) {
                        o.id = ids[o.id];
                        o.layer = ids.count(o.layer) ? ids[o.layer] : o.layer;
                        o.parent = ids.count(o.parent) ? ids[o.parent] : o.parent;
                    }
                    for (auto& s : copy.scripts) s.id = p.allocate();
                    hmi::copyOperators(p, copy.operators, src->name, copy.name);   // 1.10 : les operateurs d'un symbole
                    copy.activeLayer = ids.count(copy.activeLayer) ? ids[copy.activeLayer] : copy.layers.front().id;
                    made = copy.id;
                    p.views.push_back(std::move(copy));
                }));
                refresh();
                selectView(made);
                break;
            }
            case VDelete:
                // Lot 21 : un dossier choisi - il part, ses vues remontent d'un cran.
                if (sel == kNoId) {
                    if (const std::string f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f);
                    break;
                }
                if (onDelete_) { onDelete_(sel); break; }
                push(hmiDeleteViewCommand(doc_, sel));
                refresh();
                break;
            case VFolder:
                (void)folders_->newFolder();
                break;
            case VUp:
            case VDown:
                if (sel == kNoId) break;
                push(hmi::changeProject(doc_, "Ordre des vues", [&](hmi::Project& p) {
                    for (std::size_t i = 0; i < p.views.size(); ++i)
                        if (p.views[i].id == sel) {
                            const std::size_t j = a == VUp ? (i == 0 ? 0 : i - 1) : std::min(p.views.size() - 1, i + 1);
                            std::swap(p.views[i], p.views[j]);
                            break;
                        }
                }));
                refresh();
                selectView(sel);
                break;
            case VStart:
                if (sel == kNoId) break;
                push(hmi::changeProject(doc_, "Vue de d\xC3\xA9marrage", [&](hmi::Project& p) { p.config.startView = sel; }));
                refresh();
                selectView(sel);
                break;
            case VOpen:
                if (sel != kNoId) openView->emit(sel);
                break;
        }
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (const Id view = folders_->itemAt(r); view != kNoId) openView->emit(view);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiViewsPane::setHosts(std::function<void(const std::string&)> onNew, std::function<void(Id)> onDelete) {
    onNew_ = std::move(onNew);
    onDelete_ = std::move(onDelete);
}

void HmiViewsPane::setDuplicateReplaceHost(std::function<void(Id)> ask) { onDuplicateReplace_ = std::move(ask); }
void HmiViewsPane::setFromTypeHost(std::function<void()> ask) { onFromType_ = std::move(ask); }
void HmiViewsPane::setPackageHosts(PackageHosts h) {
    packageHosts_ = std::move(h);
    tools_->setEnabledWhen(VSaveTemplate, [this] { return selectedView() != kNoId && static_cast<bool>(packageHosts_.saveTemplate); });
}

std::string HmiViewsPane::folderTitle() const {
    switch (folder_) {
        case 0: return "Mod\xC3\xA8les";
        case 1: return "Vues";
        case 2: return "Popups";
        case 3: return "\xC3\x89" "crans mod\xC3\xA8les";
        case 4: return "En-t\xC3\xAAtes";
        case 5: return "Pieds de page";
        case 6: return "Symboles";          // lot 10
        default: return "Vues";
    }
}

std::string HmiViewsPane::folderRole() const {
    switch (folder_) {
        case 0: case 3: return "modele";
        case 2: return "popup";
        case 4: return "entete";
        case 5: return "pied";
        case 6: return "symbole";
        default: return "vue";
    }
}

void HmiViewsPane::setFolder(int folder) {
    folder_ = std::clamp(folder, -1, 6);
    const std::string role = folderRole();
    const std::string what = role == "popup" ? "Nouvelle popup" : role == "modele" ? "Nouvel \xC3\xA9" "cran mod\xC3\xA8le"
                           : role == "entete" ? "Nouvel en-t\xC3\xAAte" : role == "pied" ? "Nouveau pied de page"
                           : role == "symbole" ? "Nouveau symbole" : "Nouvelle vue";
    tools_->setText(VNew, what + (role == "vue" ? ", \xC3\xA0 la r\xC3\xA9solution du projet" : std::string{}), what);
    refresh();
}

core::CommandPtr hmiNewViewCommand(const hmi::DocumentPtr& doc, std::string name, int width, int height,
                                   std::string description, Id* made, std::string role, std::string templateKey) {
    if (!doc || name.empty()) return nullptr;
    const std::string what = role == "popup" ? "Nouvelle popup " : "Nouvelle vue ";
    return hmi::changeProject(doc, what + name, [&](hmi::Project& p) {
        auto v = hmi::makeView(p, hmi::uniqueViewName(p, name));
        if (width > 0) v.width = width;
        if (height > 0) v.height = height;
        v.description = std::move(description);
        v.role = role.empty() ? std::string("vue") : role;
        if (made) *made = v.id;
        // Lot 12 : le modele de vue choisi (un synoptique, un tableau de bord...).
        if (!templateKey.empty() && templateKey != "vide") hmi::design::fillFromTemplate(p, v, templateKey);
        // La vue de demarrage : une vue ordinaire, jamais un modele ni une popup.
        if (p.config.startView == kNoId && v.role == "vue") p.config.startView = v.id;
        p.views.push_back(std::move(v));
    });
}

core::CommandPtr hmiDeleteViewCommand(const hmi::DocumentPtr& doc, Id view) {
    if (!doc || !doc->project.view(view)) return nullptr;
    const std::string name = doc->project.view(view)->name;
    return hmi::changeProject(doc, "Supprimer la vue " + name, [&](hmi::Project& p) {
        std::erase_if(p.views, [&](const hmi::View& v) { return v.id == view; });
        if (p.config.startView == view) p.config.startView = p.views.empty() ? kNoId : p.views.front().id;
    });
}

Id HmiViewsPane::selectedView() const { return folders_->selectedItem(); }
std::vector<Id> HmiViewsPane::selectedViews() const { return folders_->selectedItems(); }

void HmiViewsPane::selectView(Id id) { folders_->selectItem(id); }

void HmiViewsPane::refresh() {
    const auto& p = doc_->project;
    std::vector<Id> shown;
    const auto show = [&](const hmi::View& v) {
        switch (folder_) {
            case -1: return true;
            case 0: return hmi::viewFolderOf(v.role) == hmi::ViewFolder::Templates;
            case 1: return hmi::viewFolderOf(v.role) == hmi::ViewFolder::Views;
            case 2: return hmi::viewFolderOf(v.role) == hmi::ViewFolder::Popups;
            default: return v.role == folderRole();
        }
    };
    for (const auto& v : p.views)
        if (show(v)) shown.push_back(v.id);
    // Lot 21 : la liste montree, rangee en dossiers - une seule liste a la fois.
    std::optional<hmi::fold::List> list;
    switch (folder_) {
        case 1: list = hmi::fold::List::Views; break;
        case 2: list = hmi::fold::List::Popups; break;
        case 3: list = hmi::fold::List::Templates; break;
        case 4: list = hmi::fold::List::Headers; break;
        case 5: list = hmi::fold::List::Footers; break;
        case 6: list = hmi::fold::List::Symbols; break;
        default: break;
    }
    // Le dossier en tete de la description : seulement a plat (Toutes, Modeles) -
    // rangees en dossiers, les lignes le montrent deja.
    const bool flat = !list.has_value();
    const auto cells = [&p, flat](Id id) -> std::vector<std::string> {
        const auto* v = p.view(id);
        if (!v) return {};
        const bool start = v->id == p.config.startView;
        std::string params;
        for (const auto& prm : v->params) params += (params.empty() ? "" : ", ") + prm.name;
        return {v->name + (start ? "   \xE2\x98\x85 d\xC3\xA9marrage" : ""), std::string(hmi::viewRoleLabel(v->role)),
                std::to_string(v->id), std::to_string(v->width) + " x " + std::to_string(v->height),
                std::to_string(v->objects.size()), std::to_string(v->layers.size()), params,
                !flat || v->folder.empty() ? v->description : "[" + v->folder + "]  " + v->description};
    };
    const auto style = [&p](Id id, std::size_t col) {
        ui::CellStyle s;
        if (col != 0) return s;
        s.icon = ui::Icon::Document;
        s.bold = id == p.config.startView;
        return s;
    };
    folders_->rebuild(list, shown, {"Vue", "R\xC3\xB4le", "Identifiant", "Taille", "Objets", "Calques", "Param\xC3\xA8tres", "Description"},
                      cells, style);
    const std::string where = folder_ < 0 ? std::string{} : " dans " + folderTitle();
    const std::size_t folders = folders_->folderCount();
    status_->setMessage(std::to_string(shown.size()) + " vue(s)" + where
                        + (folders ? ", " + std::to_string(folders) + " dossier(s)" : std::string{})
                        + ". Double-clic : ouvrir dans l'\xC3\xA9" "diteur"
                        + (list ? std::string(" ; glisse une ou plusieurs lignes sur un dossier, ou entre deux lignes.") : std::string(".")));
    invalidate();
}

void HmiViewsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    // Lot 12 : la vignette a droite de la liste.
    const float thumbW = b.w > 900.f ? std::min(460.f, b.w * 0.34f) : 0.f;
    table_->setBounds({b.x, b.y + 38, b.w - thumbW, std::max(0.f, b.h - 62)});
    if (thumb_) thumb_->setBounds({b.x + b.w - thumbW, b.y + 38, thumbW, std::max(0.f, b.h - 62)});
}

// ---------------------------------------------------------------- Rapport --
HmiReportPane::HmiReportPane(std::string id, hmi::DocumentPtr doc, Mode mode, hmi::NameExists plcHasName)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), mode_(mode), exists_(std::move(plcHasName)) {
    auto tools = std::make_unique<HmiToolStrip>(this->id() + ".tools");
    tools->add(1, mode_ == Mode::Generate ? HmiGlyph::Search : HmiGlyph::Code,
               mode_ == Mode::Generate ? "V\xC3\xA9rifier la coh\xC3\xA9rence du projet IHM"
                                       : "Compiler les expressions, les textes et les scripts",
               mode_ == Mode::Generate ? "G\xC3\xA9n\xC3\xA9rer" : "Compiler");
    // 1.10.4 : "Remplacer..." ouvre Dupliquer (0 copie) sur l'objet choisi. 1.11 (REP) : le
    // constat qu'il traite est celui d'un repere qui n'est pas une variable ($Vanne$).
    if (mode_ == Mode::Compile)
        tools->add(2, HmiGlyph::Text,
                   "Remplacer\xE2\x80\xA6 : le rep\xC3\xA8re de l'objet choisi ($Vanne$, qui n'est pas une variable) - Dupliquer "
                   "s'ouvre avec 0 copie pour le remplir dans l'original (Ctrl+Z le rend)",
                   "Remplacer\xE2\x80\xA6");
    // 1.11.18 (refonte, lot 5) : les blocs VAR qui restent dans le texte des codes - leur migration.
    if (mode_ == Mode::Compile)
        tools->add(3, HmiGlyph::Refresh,
                   "Migrer les d\xC3\xA9" "clarations\xE2\x80\xA6 : des codes d\xC3\xA9" "clarent encore leurs variables dans leur texte "
                   "(VAR \xE2\x80\xA6 END_VAR) - le rapport, les codes \xC3\xA0 cocher, une version d'abord, un seul Ctrl+Z",
                   "Migrer les d\xC3\xA9" "clarations\xE2\x80\xA6");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    if (mode_ == Mode::Compile) tools_->setEnabledWhen(2, [this] { return canReplace(); });
    if (mode_ == Mode::Compile) tools_->setVisibleWhen(3, [this] { return legacy_; });
    legacy_ = doc_ && hmi::migrate::needed(doc_->project);
    auto table = std::make_unique<ui::TableView>(this->id() + ".table");
    table->setColumns({{"Gravit\xC3\xA9", 165.f}, {"Cat\xC3\xA9gorie", 110.f}, {"Vue", 170.f}, {"Objet", 170.f},
                       {"Propri\xC3\xA9t\xC3\xA9 / script", 190.f}, {"Message", 540.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(this->id() + ".status")));
    links_ += tools_->triggered->connect([this](int action) {
        if (action == 2) replaceSelected();      // 1.10.4
        else if (action == 3) migrateRequested->emit();   // 1.11.18 (lot 5)
        else run();
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r >= issues_.size()) return;
        goTo->emit(issues_[r].view, issues_[r].object);
        issueActivated->emit(issues_[r]);
    });
    status_->setMessage(mode_ == Mode::Generate ? "G\xC3\xA9n\xC3\xA9rer : lance la v\xC3\xA9rification."
                                                : "Compiler : lance la compilation.");
}

bool HmiReportPane::canReplace() const {
    if (mode_ != Mode::Compile || !table_) return false;
    const auto rows = table_->selectedModelRows();
    if (rows.size() != 1 || rows.front() >= issues_.size()) return false;
    const auto& i = issues_[rows.front()];
    return i.object != hmi::kNoId && i.view != hmi::kNoId && hmi::dup::isUnreplacedIssueMessage(i.message);
}

bool HmiReportPane::replaceSelected() {
    if (!canReplace()) return false;
    const auto& i = issues_[table_->selectedModelRows().front()];
    replaceMarkers->emit(i.view, i.object);
    return true;
}

void HmiReportPane::run() {
    const auto t0 = std::chrono::steady_clock::now();
    legacy_ = doc_ && hmi::migrate::needed(doc_->project);   // 1.11.18 (lot 5) : le bouton Migrer
    if (mode_ == Mode::Generate) {
        // Lot 13 : les textes qui debordent se mesurent avec les polices de l'ecran.
        hmi::GenerateOptions opt;
        opt.projectFolder = hmiProjectFolder();
        opt.measure = hmiTextMeasure(doc_->project);
        // Lot 14 : le plan d'adressage Modbus.
        hmi::comm::Plan plan;
        if (commPlan_) {
            plan = commPlan_();
            opt.plan = &plan;
        }
        opt.plcScalar = plcScalar_;
        opt.plcPaths = plcPaths_;   // ---- Lot API 8 : les expressions impossibles ----
        issues_ = hmi::generateWith(doc_->project, exists_, opt);
    } else {
        // ---- Lot API 8 : les expressions impossibles ---- (les noms de l'automate :
        // un nom inconnu, un membre, une fonction, un type impossibles sont des erreurs)
        issues_ = hmi::compileWith(doc_->project, exists_, plcPaths_);
    }
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    // Lot 13 : les erreurs d'abord, et chaque ligne a la couleur de sa gravite.
    hmikit::sortIssues(issues_);
    std::vector<std::vector<std::string>> rows;
    std::vector<hmi::Issue::Severity> severities;
    for (const auto& i : issues_) {
        const auto* v = doc_->project.view(i.view);
        const auto* o = v ? v->object(i.object) : nullptr;
        // Un script : sa ligne a cote de son nom ("Demarrage, ligne 4").
        // Un script de vue : son evenement (la vue a sa colonne).
        std::string where = i.property;
        if (const auto* sc = i.script != hmi::kNoId ? doc_->project.script(i.script) : nullptr; sc && v) where = sc->event;
        if ((i.script != hmi::kNoId || i.category == "Fonction") && i.line > 0) {
            where += ", ligne " + std::to_string(i.line);
            if (i.column > 0) where += ", col. " + std::to_string(i.column);   // 1.10 : la colonne de la faute
        }
        rows.push_back({std::string(hmi::toString(i.severity)), i.category, v ? v->name : "", o ? o->name : "",
                        std::move(where), i.message});
        severities.push_back(i.severity);
    }
    model_ = std::make_shared<hmikit::IssueRows>(std::vector<std::string>{"Gravit\xC3\xA9", "Cat\xC3\xA9gorie", "Vue", "Objet",
                                                                          "Propri\xC3\xA9t\xC3\xA9 / script", "Message"},
                                                 std::move(rows), std::move(severities), 5);
    table_->setModel(model_);
    const auto c = hmi::count(issues_);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%zu erreur(s), %zu avertissement(s), %zu information(s)  \xC2\xB7  %.1f ms",
                  c.errors, c.warnings, c.infos, ms);
    status_->setMessage(buf, c.errors ? ui::StatusBar::Severity::Error
                           : c.warnings ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
    invalidate();
    ran->emit();   // ---- Lot API 8 : l'arbre du projet (la pastille rouge des expressions impossibles) ----
}

void HmiReportPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    table_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
    // Lot 13 : le message va jusqu'au bord - la couleur de la gravite teinte toute la ligne.
    if (b.w != laidOutWidth_) {
        laidOutWidth_ = b.w;
        const float message = std::max(540.f, b.w - (165.f + 110.f + 170.f + 170.f + 190.f) - 18.f);
        table_->setColumns({{"Gravit\xC3\xA9", 165.f}, {"Cat\xC3\xA9gorie", 110.f}, {"Vue", 170.f}, {"Objet", 170.f},
                            {"Propri\xC3\xA9t\xC3\xA9 / script", 190.f}, {"Message", message}});
    }
}

// ------------------------------------------------------------------- Info --
HmiInfoPane::HmiInfoPane(std::string id, std::string title, std::vector<std::string> lines)
    : ui::Widget(std::move(id)), title_(std::move(title)), lines_(std::move(lines)) {}

void HmiInfoPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    float y = b.y + 24;
    ctx.r.drawText({b.x + 28, y}, title_, ctx.theme.font.title, ctx.theme.color.text);
    y += ctx.r.lineHeight(ctx.theme.font.title) + 14;
    for (const auto& l : lines_) {
        const bool bullet = !l.empty() && l[0] == '-';
        ctx.r.drawText({b.x + (bullet ? 40.f : 28.f), y}, bullet ? "\xE2\x80\xA2 " + l.substr(1) : l, ctx.theme.font.ui,
                       l.empty() ? ctx.theme.color.text : bullet ? ctx.theme.color.text : ctx.theme.color.textMuted);
        y += ctx.r.lineHeight(ctx.theme.font.ui) + 6;
    }
}

} // namespace app
