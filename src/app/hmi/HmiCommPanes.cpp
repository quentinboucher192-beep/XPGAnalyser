// Configuration > Equipements : l'automate du projet (lot 14) ; le volet, ses
// onglets et sa barre (lot 15). Les equipements, les variables liees, le
// reseau du PC et le scanner : HmiEquipmentPanes.cpp.
#include "HmiCommPanes.hpp"
#include "../ExportTarget.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiCommHost.hpp"
#include "HmiEquipmentHost.hpp"
#include "HmiMemoryMap.hpp"
#include "HmiTwinValues.hpp"
#include "HmiNetDiagram.hpp"
#include "HmiSimMarks.hpp"               // 1.9 : le violet du simule
#include "../../platform/Renderer.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../sim/Runtime.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using hmi::Id;
using PG = ui::PropertyGrid;

namespace {

enum : int {
    CTest = 1, CAdd, CRemove, CPropose, CExport, CReconnect, CMute, CUnmute,
    // lot 15
    CRefreshNet, CTestAll, CAddEquip, CPing, CTool, CRestore, CRemoveEquip, CTestEquip, CBind, CUnbind,
    CScanStart, CScanStop, CScanAdd, CScanExport,
    // lot 16
    COpenVar,
    // lot 17
    CAddVirtual, CCreateTwin, CDetect, CTwinsStart, CTwinsStop, COpenMap,
    CMapScan, CMapStop, CMapCreate, CMapPropose, CMapExport, CTwinRestart, CTwinSave, CTwinExport, CTwinImport,
    CSimPortAdd, CSimCopy, CSimPortRemove,
    // lot 18
    CValAnimateAll, CValStopAll, CValUnforce, CValAdd,
};

ui::Tone toneOf(int t) {
    switch (t) {
        case 1: return ui::Tone::Ok;
        case 2: return ui::Tone::Warning;
        case 3: return ui::Tone::Error;
        case 4: return ui::Tone::Accent;
        default: return ui::Tone::None;
    }
}

// Lot 16 : l'onglet Plan d'adressage - "Plan de" au-dessus du tableau.
class PlanPage final : public ui::Widget {
public:
    explicit PlanPage(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 2) return;
        constexpr float kBar = 36.f;
        children()[0]->setBounds({b.x + 70.f, b.y + 5.f, 380.f, 26.f});
        children()[1]->setBounds({b.x, b.y + kBar, b.w, std::max(0.f, b.h - kBar)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 36.f}, ctx.theme.color.panelBg);
        ctx.r.drawText({b.x + 10.f, b.y + (36.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, "Plan de", ctx.theme.font.ui, ctx.theme.color.textMuted);
    }
};

// Lot 17 : l'onglet Carte memoire - "Carte de", "Montrer", "Valeurs", replier,
// Chercher, au-dessus de la carte.
class MapPage final : public ui::Widget {
public:
    explicit MapPage(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 6) return;
        constexpr float kBar = 38.f;
        float x = b.x + 76.f;
        children()[0]->setBounds({x, b.y + 6.f, 290.f, 26.f});           // Carte de
        x += 290.f + 76.f;
        children()[1]->setBounds({x, b.y + 6.f, 215.f, 26.f});           // Montrer
        x += 215.f + 70.f;
        children()[2]->setBounds({x, b.y + 6.f, 118.f, 26.f});           // Valeurs
        x += 128.f;
        children()[3]->setBounds({x, b.y + 6.f, 146.f, 26.f});           // replier le vide
        x += 150.f;
        children()[4]->setBounds({x, b.y + 6.f, std::max(90.f, b.x + b.w - x - 8.f), 26.f});    // Chercher
        children()[5]->setBounds({b.x, b.y + kBar, b.w, std::max(0.f, b.h - kBar)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 38.f}, ctx.theme.color.panelBg);
        const float y = b.y + (38.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;
        ctx.r.drawText({b.x + 10.f, y}, "Carte de", ctx.theme.font.ui, ctx.theme.color.textMuted);
        if (children().size() >= 3) {
            ctx.r.drawText({children()[1]->bounds().x - 68.f, y}, "Montrer", ctx.theme.font.ui, ctx.theme.color.textMuted);
            ctx.r.drawText({children()[2]->bounds().x - 62.f, y}, "Valeurs", ctx.theme.font.ui, ctx.theme.color.textMuted);
        }
    }
};

// Lot 17 : l'onglet Equipements - "Montrer" et le resume, au-dessus du tableau.
class EquipPage final : public ui::Widget {
public:
    explicit EquipPage(std::string id) : ui::Widget(std::move(id)) {}
    std::function<std::string()> summary;
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 2) return;
        children()[0]->setBounds({b.x + 76.f, b.y + 5.f, 230.f, 26.f});
        children()[1]->setBounds({b.x, b.y + 36.f, b.w, std::max(0.f, b.h - 36.f)});
        // 1.9 : la case "Reperer les lectures simulees", a droite de la barre.
        if (children().size() > 2) {
            const float w = std::min(260.f, children()[2]->sizeHint().preferred.w + 8.f);
            children()[2]->setBounds({b.right() - w - 8.f, b.y + 5.f, w, 26.f});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 36.f}, ctx.theme.color.panelBg);
        const float y = b.y + (36.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;
        ctx.r.drawText({b.x + 10.f, y}, "Montrer", ctx.theme.font.ui, ctx.theme.color.textMuted);
        const float right = children().size() > 2 ? children()[2]->bounds().x - 12.f : b.right() - 8.f;
        if (summary) ctx.r.drawText({b.x + 324.f, y}, simmark::fitText(ctx.r, summary(), ctx.theme.font.ui, right - b.x - 324.f), ctx.theme.font.ui,
                                    ctx.theme.color.textMuted);
    }
};

// Lot 16 : le plan d'adressage - des groupes (une liaison), des lignes, une infobulle par ligne.
class PlanModel final : public ui::ITableModel {
public:
    struct Line {
        std::vector<std::string> cells;
        int   tone{0};
        int   kind{0};           // 0 groupe, 1 automate, 2 variable IHM, 3 case d'une structure IHM, 4 refusee
        int   expander{-1};
        float indent{0.f};
        std::string tip;
    };
    explicit PlanModel(std::vector<Line> lines) : lines_(std::move(lines)) {}
    [[nodiscard]] std::size_t rowCount() const override { return lines_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 8; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Variable", "Adresse", "Type", "Origine", "\xC3\x89quipement", "Place Modbus", "Fonctions", "Qualit\xC3\xA9 (en marche)"};
        return c < 8 ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < lines_.size() && c < lines_[r].cells.size() ? lines_[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= lines_.size()) return st;
        const auto& l = lines_[r];
        if (l.kind == 0) {
            st.spanRow = c == 0;
            st.bold = true;
            st.bg = gfx::Color{38, 43, 51, 255};
            st.expander = c == 0 ? l.expander : -1;
            st.icon = c == 0 ? (l.cells.size() > 1 && l.cells[1] == "plc" ? ui::Icon::Cpu : ui::Icon::Module) : ui::Icon::None;
            st.iconTone = toneOf(l.tone);
            return st;
        }
        if (c == 0) {
            st.bold = l.kind != 3;
            st.indent = l.indent;
            st.expander = l.expander;
            if (l.kind == 2 || l.kind == 3) {
                st.icon = l.kind == 2 ? ui::Icon::Screen : ui::Icon::None;
                st.iconTone = ui::Tone::Accent;
                if (l.kind == 2) {
                    st.badge = "IHM";
                    st.badgeTone = ui::Tone::Accent;
                }
            } else {
                st.icon = ui::Icon::Variable;
                st.iconTone = l.tone == 3 ? ui::Tone::Error : ui::Tone::Ok;
            }
        }
        if (c == 3 && l.kind == 4) st.fgTone = ui::Tone::Error;
        if (c == 3 && (l.kind == 2 || l.kind == 3)) st.fgTone = l.kind == 3 ? ui::Tone::Muted : ui::Tone::Accent;
        if (c == 4 && l.kind == 3) st.fgTone = ui::Tone::Muted;
        if (c == 1) st.monospace = true;
        if (c == 7) st.fgTone = toneOf(l.tone);
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < lines_.size() ? lines_[r].tip : std::string{}; }
private:
    std::vector<Line> lines_;
};

const char* const kTypes[] = {"", "BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "TIME", "STRING"};

std::string modeLabel(const hmi::Communication& c) { return c.modbus() ? "Modbus TCP" : "Simulateur"; }
std::string orderLabel(const hmi::Communication& c) {
    return c.wordOrder == "fort" ? "poids fort d'abord" : "poids faible d'abord (Schneider)";
}

bool validPath(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    int depth = 0;
    for (const char c : s) {
        if (c == '[') { if (++depth > 1) return false; continue; }
        if (c == ']') { if (--depth < 0) return false; continue; }
        if (depth > 0) { if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false; continue; }
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return depth == 0;
}


// L'heure d'une heure murale (s depuis 1970) : "21:32:05".
std::string clockOf(double epoch) {
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string upperOf(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

HmiCommPane::HmiCommPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    // Lot 15 : le reseau du PC, les equipements.
    tools->add(CRefreshNet, HmiGlyph::Refresh, "Actualiser : relire les ports du PC et leurs adresses", "Actualiser");
    tools->add(CTestAll, HmiGlyph::Check, "Tester tous les \xC3\xA9quipements : un ping (et leur port), tout de suite", "Tester tous les \xC3\xA9quipements");
    tools->add(CScanStart, HmiGlyph::Search, "Scanner : essayer chaque adresse de la plage (ping, ARP, ports TCP, Modbus)", "Scanner");
    tools->add(CScanStop, HmiGlyph::Stop, "Arr\xC3\xAAter le scan", "Arr\xC3\xAAter");
    tools->separator();
    tools->add(CAddEquip, HmiGlyph::Plus, "Ajouter un \xC3\xA9quipement (Modbus TCP/IP ou Ethernet TCP/IP), dans le r\xC3\xA9seau du port choisi",
               "\xC3\x89quipement");
    tools->add(CScanAdd, HmiGlyph::Plus, "Ajouter comme \xC3\xA9quipement l'adresse choisie (Modbus TCP/IP si le port 502 r\xC3\xA9pond)",
               "Ajouter comme \xC3\xA9quipement");
    tools->add(CAddVirtual, HmiGlyph::Plus, "Ajouter un esclave virtuel : un \xC3\xA9quipement seulement simul\xC3\xA9 (pas encore d'appareil), un esclave Modbus dans l'application",
               "Esclave virtuel");
    tools->add(CCreateTwin, HmiGlyph::Duplicate,
               "Cloner en esclave simul\xC3\xA9 : un esclave Modbus dans l'application, avec la configuration du vrai appareil choisi, li\xC3\xA9 \xC3\xA0 lui "
               "(le cadenas) - l'IHM le lit quand le vrai ne r\xC3\xA9pond pas",
               "Cloner en esclave simul\xC3\xA9");
    tools->add(CRemoveEquip, HmiGlyph::Delete,
               "Supprimer l'\xC3\xA9quipement choisi (Suppr) : ses variables IHM \xC3\xA0 cocher, son esclave simul\xC3\xA9 ; la ligne d'un esclave : le retirer. Ctrl+Z rend tout",
               "Supprimer");
    tools->add(CTestEquip, HmiGlyph::Check, "Tester l'\xC3\xA9quipement choisi : connexion, identification, lecture de ses variables (ou ping et port)",
               "Tester");
    tools->add(CBind, HmiGlyph::Plus, "Lier une variable IHM \xC3\xA0 l'\xC3\xA9quipement choisi (\xC3\xA0 la prochaine adresse libre)", "Lier une variable");
    tools->add(COpenVar, HmiGlyph::Search, "Ouvrir la variable IHM choisie dans Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM (double-clic)",
               "Ouvrir la variable");
    tools->add(CUnbind, HmiGlyph::Delete, "D\xC3\xA9lier la variable choisie : elle redevient une variable IHM locale", "D\xC3\xA9lier");
    tools->add(CPing, HmiGlyph::Search, "Ping : l'outil Modbus, onglet Ping, vers l'\xC3\xA9quipement choisi", "Ping\xE2\x80\xA6");
    tools->add(CDetect, HmiGlyph::Search, "D\xC3\xA9tecter les zones : lire (seulement lire) l'\xC3\xA9quipement pour trouver ses zones m\xC3\xA9moire et leurs trous",
               "D\xC3\xA9tecter les zones");
    tools->add(CTwinsStart, HmiGlyph::Play, "D\xC3\xA9marrer les esclaves simul\xC3\xA9s : chacun r\xC3\xA9pond", "D\xC3\xA9marrer les esclaves");
    tools->add(CTwinsStop, HmiGlyph::Stop, "Arr\xC3\xAAter les esclaves simul\xC3\xA9s : ils ne r\xC3\xA9pondent plus (l'IHM qui les lit les voit injoignables)", "Arr\xC3\xAAter");
    tools->add(CTool, HmiGlyph::Gear, "Outil Modbus : lire, \xC3\xA9" "crire, trames, espion, vers l'\xC3\xA9quipement choisi", "Outil Modbus");
    tools->add(COpenMap, HmiGlyph::Search, "Carte m\xC3\xA9moire de l'\xC3\xA9quipement choisi : ses variables case par case, les chevauchements", "Carte m\xC3\xA9moire");
    tools->add(CMapScan, HmiGlyph::Play, "Scanner l'\xC3\xA9quipement : ses zones lues en boucle (lecture seule), pour voir o\xC3\xB9 il a de l'activit\xC3\xA9", "Scanner");
    tools->add(CMapStop, HmiGlyph::Stop, "Arr\xC3\xAAter le scan : ce qui a \xC3\xA9t\xC3\xA9 vu reste sur la carte", "Arr\xC3\xAAter le scan");
    tools->add(CMapCreate, HmiGlyph::Plus, "Cr\xC3\xA9" "er une variable IHM ici : li\xC3\xA9" "e \xC3\xA0 la case choisie (du type vu)", "Cr\xC3\xA9" "er une variable ici");
    tools->add(CMapPropose, HmiGlyph::Refresh, "Proposer une adresse libre \xC3\xA0 la variable choisie : dans les zones, sans chevauchement", "Adresse libre");
    tools->add(CMapExport, HmiGlyph::Export, "Exporter la carte (Excel, dossier exports/) : chaque variable, sa place, qui l'\xC3\xA9" "crit, ses chevauchements", "Exporter la carte");
    tools->add(CTwinRestart, HmiGlyph::Undo, "Remettre la m\xC3\xA9moire de l'esclave simul\xC3\xA9 \xC3\xA0 son d\xC3\xA9part (z\xC3\xA9ros, valeurs initiales, ou la m\xC3\xA9moire gard\xC3\xA9" "e)",
               "M\xC3\xA9moire au d\xC3\xA9part");
    tools->add(CTwinSave, HmiGlyph::Check, "Garder cette m\xC3\xA9moire : l'esclave simul\xC3\xA9 repartira d'elle \xC3\xA0 chaque d\xC3\xA9marrage", "Garder la m\xC3\xA9moire");
    tools->add(CTwinExport, HmiGlyph::Export, "Exporter la m\xC3\xA9moire de l'esclave simul\xC3\xA9 (CSV : table;adresse;valeur)", "Exporter (CSV)");
    tools->add(CTwinImport, HmiGlyph::Plus, "Importer une m\xC3\xA9moire (CSV : table;adresse;valeur) dans l'esclave simul\xC3\xA9", "Importer (CSV)");
    // Lot 18 : les valeurs simulees.
    tools->add(CValAnimateAll, HmiGlyph::Play, "Tout animer : chaque mouvement arr\xC3\xAAt\xC3\xA9 reprend (ses r\xC3\xA9glages sont gard\xC3\xA9s)", "Tout animer");
    tools->add(CValStopAll, HmiGlyph::Stop, "Tout arr\xC3\xAAter : plus rien ne bouge tout seul (les r\xC3\xA9glages restent)", "Tout arr\xC3\xAAter");
    tools->add(CValUnforce, HmiGlyph::Undo, "D\xC3\xA9" "forcer tout : chaque case forc\xC3\xA9" "e redevient libre (les \xC3\xA9" "critures y passent de nouveau)", "D\xC3\xA9" "forcer tout");
    tools->add(CValAdd, HmiGlyph::Plus, "Ajouter un registre anim\xC3\xA9, sans variable IHM : la premi\xC3\xA8re case libre des zones de l'esclave simul\xC3\xA9 choisi", "Ajouter un registre");
    tools->add(CSimPortAdd, HmiGlyph::Plus, "Ajouter un port au PC simul\xC3\xA9 (rien ne change sur le vrai PC)", "Port simul\xC3\xA9");
    tools->add(CSimCopy, HmiGlyph::Refresh, "Copier le r\xC3\xA9seau du PC : un port simul\xC3\xA9 par r\xC3\xA9seau du vrai PC (les esclaves simul\xC3\xA9s gardent l'adresse de leur appareil)",
               "Copier le r\xC3\xA9seau du PC");
    tools->add(CSimPortRemove, HmiGlyph::Delete, "Retirer le port simul\xC3\xA9 choisi", "Retirer le port simul\xC3\xA9");
    tools->add(CScanExport, HmiGlyph::Export, "Exporter le r\xC3\xA9sultat du scan (CSV, dossier exports/)", "Exporter le scan");
    tools->add(CRestore, HmiGlyph::Undo,
               "Remettre l'adresse d'avant : le port revient au r\xC3\xA9glage qu'il avait avant le dernier changement", "Remettre l'adresse d'avant");
    tools->separator();
    // Lot 14 : l'automate du projet.
    tools->add(CTest, HmiGlyph::Check, "Tester : se connecter \xC3\xA0 l'automate avec ces r\xC3\xA9glages, lire son identification et les variables du plan",
               "Tester l'automate");
    tools->add(CAdd, HmiGlyph::Plus, "Ajouter une variable \xC3\xA0 la table des adresses (\xC3\xA0 la prochaine adresse libre)", "Ajouter");
    tools->add(CRemove, HmiGlyph::Delete, "Retirer la variable choisie de la table des adresses", "Retirer la ligne");
    tools->add(CPropose, HmiGlyph::Search,
               "Proposer : les variables de l'automate que l'IHM utilise sans adresse Modbus, chacune \xC3\xA0 une adresse libre "
               "(le programme doit les y recopier)",
               "Proposer");
    tools->add(CExport, HmiGlyph::Export, "Exporter le plan d'adressage en Excel (dossier exports/), pour l'automaticien", "Exporter");
    tools->separator();
    tools->add(CReconnect, HmiGlyph::Refresh, "Reconnecter : la liaison se refait tout de suite", "Reconnecter");
    tools->add(CMute, HmiGlyph::Stop, "Couper le serveur de d\xC3\xA9monstration : il ne r\xC3\xA9pond plus (un c\xC3\xA2" "ble d\xC3\xA9" "branch\xC3\xA9)",
               "Couper la d\xC3\xA9mo");
    tools->add(CUnmute, HmiGlyph::Play, "R\xC3\xA9tablir le serveur de d\xC3\xA9monstration", "R\xC3\xA9tablir");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // La barre suit l'onglet.
    const auto on = [this](std::initializer_list<int> tabs) {
        const std::vector<int> list(tabs);
        return [this, list] {
            const int t = static_cast<int>(tabs_ ? tabs_->currentIndex() : 0);
            return std::find(list.begin(), list.end(), t) != list.end();
        };
    };
    // Lot 17 : le Reseau du PC, reel ou simule.
    const auto real = [this, on](std::initializer_list<int> tabs) {
        const auto f = on(tabs);
        return [this, f] { return f() && !(static_cast<int>(tabs_ ? tabs_->currentIndex() : 0) == TNetwork && simView_); };
    };
    const auto simNet = [this] { return static_cast<int>(tabs_ ? tabs_->currentIndex() : 0) == TNetwork && simView_; };
    tools_->setVisibleWhen(CRefreshNet, real({TNetwork}));
    tools_->setVisibleWhen(CTestAll, real({TNetwork, TState}));
    tools_->setVisibleWhen(CScanStart, on({TScanner}));
    tools_->setVisibleWhen(CScanStop, on({TScanner}));
    tools_->setVisibleWhen(CAddEquip, real({TNetwork, TEquipments}));
    tools_->setVisibleWhen(CScanAdd, on({TScanner}));
    tools_->setVisibleWhen(CRemoveEquip, on({TEquipments, TNetwork}));
    tools_->setVisibleWhen(CAddVirtual, [on, simNet] { return on({TEquipments})() || simNet(); });
    tools_->setVisibleWhen(CCreateTwin, on({TEquipments, TNetwork}));
    tools_->setVisibleWhen(CDetect, [this, on] { return on({TEquipments})() || (on({TMap})() && !mapTwin_); });
    tools_->setVisibleWhen(CTwinsStart, [on, simNet] { return on({TEquipments})() || simNet(); });
    tools_->setVisibleWhen(CTwinsStop, [on, simNet] { return on({TEquipments})() || simNet(); });
    tools_->setVisibleWhen(COpenMap, on({TEquipments, TNetwork, TBound, TValues}));
    tools_->setVisibleWhen(CValAnimateAll, on({TValues}));
    tools_->setVisibleWhen(CValStopAll, on({TValues}));
    tools_->setVisibleWhen(CValUnforce, on({TValues}));
    tools_->setVisibleWhen(CValAdd, on({TValues}));
    tools_->setEnabledWhen(CValUnforce, [this] {
        for (const auto& e : doc_->project.equipments)
            if (!e.forcings.empty()) return true;
        return false;
    });
    tools_->setVisibleWhen(CMapScan, on({TMap}));
    tools_->setVisibleWhen(CMapStop, on({TMap}));
    tools_->setVisibleWhen(CMapCreate, on({TMap}));
    tools_->setVisibleWhen(CMapPropose, on({TMap}));
    tools_->setVisibleWhen(CMapExport, on({TMap}));
    const auto mapTwin = [this, on] { return on({TMap})() && mapTwin_; };
    tools_->setVisibleWhen(CTwinRestart, mapTwin);
    tools_->setVisibleWhen(CTwinSave, mapTwin);
    tools_->setVisibleWhen(CTwinExport, mapTwin);
    tools_->setVisibleWhen(CTwinImport, mapTwin);
    tools_->setVisibleWhen(CSimPortAdd, simNet);
    tools_->setVisibleWhen(CSimCopy, simNet);
    tools_->setVisibleWhen(CSimPortRemove, simNet);
    tools_->setEnabledWhen(CSimPortRemove, [this] { return portSide_ && !simPort_.empty(); });
    tools_->setEnabledWhen(CCreateTwin, [this] {
        const auto* e = equipmentOf(equipment_);
        return e && e->modbus() && !e->hasTwin();
    });
    tools_->setEnabledWhen(CDetect, [this] {
        const int tab = static_cast<int>(tabs_->currentIndex());
        if (tab == TMap) return !mapEquip_.empty() && !detecting();
        const auto* e = equipmentOf(equipment_);
        return e && e->modbus() && !detecting();
    });
    tools_->setEnabledWhen(COpenMap, [this] {
        if (static_cast<int>(tabs_->currentIndex()) == TBound) return !bound_.empty();
        if (static_cast<int>(tabs_->currentIndex()) == TValues) return valuesCtl_ && !valuesCtl_->selectedEquipment().empty();
        if (equipment_ == kPlcKey) return true;
        const auto* e = equipmentOf(equipment_);
        return e && e->modbus();
    });
    tools_->setEnabledWhen(CMapScan, [this] { return !mapScanning() && !mapEquip_.empty(); });
    tools_->setEnabledWhen(CMapStop, [this] { return mapScanning(); });
    tools_->setEnabledWhen(CMapCreate, [this] {
        const auto s = map_ ? map_->selected() : std::nullopt;
        return s.has_value() && mapEquip_ != kPlcKey && map_->map().at(s->table, s->offset).empty();
    });
    tools_->setEnabledWhen(CMapPropose, [this] {
        const auto s = map_ ? map_->selected() : std::nullopt;
        return s.has_value() && mapEquip_ != kPlcKey && !map_->map().at(s->table, s->offset).empty();
    });
    // Lot 16 : dans le plan d'adressage, la barre suit la ligne choisie - l'automate
    // (Tester l'automate) ou un equipement (Tester, Ping, Reconnecter CET equipement,
    // Ouvrir la variable IHM, Delier).
    const auto planEquip = [this] {
        const auto* r = static_cast<int>(tabs_ ? tabs_->currentIndex() : 0) == TPlan ? selectedPlanRow() : nullptr;
        return r && r->group != kPlcKey;
    };
    const auto planIhm = [this] {
        const auto* r = static_cast<int>(tabs_ ? tabs_->currentIndex() : 0) == TPlan ? selectedPlanRow() : nullptr;
        return r && (r->kind == PlanRow::Kind::Ihm || r->kind == PlanRow::Kind::Member);
    };
    tools_->setVisibleWhen(CTestEquip, [on, planEquip] { return on({TEquipments, TTest})() || planEquip(); });
    tools_->setVisibleWhen(CBind, on({TBound, TEquipments}));
    tools_->setVisibleWhen(CUnbind, [on, planIhm] { return on({TBound})() || planIhm(); });
    tools_->setVisibleWhen(COpenVar, [on, planIhm] { return on({TBound, TMap})() || planIhm(); });
    tools_->setVisibleWhen(CPing, [real, planEquip] { return real({TNetwork})() || planEquip(); });
    tools_->setVisibleWhen(CTool, on({TNetwork, TScanner, TEquipments, TBound, TMap, TValues}));
    tools_->setVisibleWhen(CScanExport, on({TScanner}));
    tools_->setVisibleWhen(CRestore, real({TNetwork}));
    tools_->setVisibleWhen(CTest, [on, planEquip] { return on({TTable, TPlan, TTest})() && !planEquip(); });
    tools_->setVisibleWhen(CAdd, on({TTable}));
    tools_->setVisibleWhen(CRemove, on({TTable}));
    tools_->setVisibleWhen(CPropose, on({TTable}));
    tools_->setVisibleWhen(CExport, on({TTable, TPlan, TBound}));
    tools_->setVisibleWhen(CReconnect, on({TTable, TPlan, TState}));
    tools_->setVisibleWhen(CMute, on({TTable, TState}));
    tools_->setVisibleWhen(CUnmute, on({TTable, TState}));
    tools_->setEnabledWhen(CRemove, [this] { return !selectedAddress().empty(); });
    tools_->setEnabledWhen(COpenVar, [this] {
        if (static_cast<int>(tabs_->currentIndex()) == TMap) {
            const auto s = map_ ? map_->selected() : std::nullopt;
            return hosts_.openVariable && s.has_value() && !map_->map().at(s->table, s->offset).empty();
        }
        return hosts_.openVariable && !bound_.empty();
    });
    tools_->setEnabledWhen(CReconnect, [this] {
        const int tab = static_cast<int>(tabs_->currentIndex());
        if ((tab == TEquipments || tab == TPlan) && !equipment_.empty() && equipment_ != kPlcKey)
            return host() && host()->link(equipment_) != nullptr;
        return hosts_.comm && hosts_.comm() && hosts_.comm()->link();
    });
    tools_->setEnabledWhen(CMute, [this] { return hosts_.comm && hosts_.comm() && hosts_.comm()->running() && !hosts_.comm()->demoMute(); });
    tools_->setEnabledWhen(CUnmute, [this] { return hosts_.comm && hosts_.comm() && hosts_.comm()->demoMute(); });
    tools_->setEnabledWhen(CRemoveEquip, [this] {
        if (static_cast<int>(tabs_->currentIndex()) == TNetwork && portSide_) return false;
        return !equipment_.empty() && equipment_ != kPlcKey;
    });
    tools_->setEnabledWhen(CTestEquip, [this] { return !equipment_.empty(); });
    tools_->setEnabledWhen(CUnbind, [this] { return !bound_.empty(); });
    tools_->setEnabledWhen(CScanStart, [this] { return !scanning(); });
    tools_->setEnabledWhen(CScanStop, [this] { return scanning(); });
    tools_->setEnabledWhen(CScanAdd, [this] { return !selectedScanned().empty(); });
    tools_->setEnabledWhen(CScanExport, [this] { return scanner_ && !scanner_->results().empty(); });
    tools_->setEnabledWhen(CRestore, [this] {
        auto* h = host();
        return h && !h->applyState().busy && h->previous(port_.empty() ? h->lastChangedAdapter() : port_).has_value();
    });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto diagram = std::make_unique<HmiNetDiagram>(base + ".network");
    diagram_ = diagram.get();
    auto scan = std::make_unique<HmiScanPage>(base + ".scanner");
    scanPage_ = scan.get();
    scanPage_->table().setColumns({{"Adresse", 150.f}, {"Dans le projet", 170.f}, {"MAC", 160.f}, {"Fabricant", 230.f}, {"Ports ouverts", 170.f},
                                   {"Modbus (identification)", 330.f}, {"Ping", 80.f}, {"Nom", 140.f}, {"Vu par", 70.f}});
    scanPage_->table().setSelectionMode(ui::SelectionMode::Single);
    // Lot 17 : un seul onglet - le vrai appareil, son jumeau, ce que l'IHM utilise en simulation.
    auto equipPage = std::make_unique<EquipPage>(base + ".equipPage");
    equipPage->summary = [this] { return equipSummary_; };
    equipFilterBox_ = &static_cast<ui::DropDown&>(equipPage->addChild(std::make_unique<ui::DropDown>(base + ".equipFilter")));
    // 1.9 : Montrer - tous, les vrais appareils, les esclaves simules (lies et
    // seulement simules), ceux que l'IHM lit en simule.
    equipFilterBox_->setItems({{"tous les \xC3\xA9quipements", "tous", {}, true}, {"les vrais appareils", "vrais", {}, true},
                               {"les esclaves simul\xC3\xA9s", "esclaves", {}, true}, {"lus en simul\xC3\xA9", "lus", {}, true}});
    equipFilterBox_->setSelectedIndex(0);
    auto equip = std::make_unique<ui::TableView>(base + ".equipments");
    equip->setColumns({{"\xC3\x89quipement", 250.f}, {"Type", 120.f}, {"Adresse", 250.f}, {"\xC3\x89tat", 200.f},
                       {"L'IHM lit", 220.f}, {"Var.", 54.f}, {"Zones m\xC3\xA9moire", 230.f}, {"Description", 220.f}});
    equip->setSelectionMode(ui::SelectionMode::Single);
    // 1.9 : les icones des lignes simulees (la fiole, le coude et le cadenas, le point).
    equip->setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) {
        if (icon == kIconFlask) {
            simmark::drawFlask(r, box, color);
        } else if (icon == kIconSlave) {
            // Le coude (la ligne vient du vrai, au-dessus), le cadenas, la fiole : dans
            // le retrait de la ligne (kSlaveIndent, et les 16 px de la fleche de la table).
            const float s = std::min(box.h, 16.f), mid = box.y + box.h * 0.5f;
            const float x0 = box.x - kSlaveIndent - 4.f;
            r.line({x0, box.y - 4.f}, {x0, mid}, color, 1.2f);
            r.line({x0, mid}, {x0 + 8.f, mid}, color, 1.2f);
            simmark::drawLockBadge(r, {x0 + 12.f, mid - s * 0.5f, s, s});
            simmark::drawFlask(r, box, color);
        } else if (icon == kIconDot) {
            simmark::drawDot(r, {box.x + box.w * 0.5f, box.y + box.h * 0.5f}, std::min(4.f, box.h * 0.3f));
        }
    });
    auto bound = std::make_unique<ui::TableView>(base + ".bound");
    bound->setColumns({{"Variable", 180.f}, {"\xC3\x89quipement", 170.f}, {"Adresse", 100.f}, {"Place Modbus", 190.f}, {"Type", 70.f},
                       {"Mise \xC3\xA0 l'\xC3\xA9" "chelle", 170.f}, {"Acc\xC3\xA8s", 110.f}, {"Valeur", 110.f}, {"Qualit\xC3\xA9", 240.f}});
    bound->setSelectionMode(ui::SelectionMode::Single);
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Variable", 250.f}, {"Adresse", 84.f}, {"Type", 70.f}, {"Acc\xC3\xA8s", 132.f}, {"Place Modbus", 170.f},
                       {"Fonctions", 84.f}, {"\xC3\x89tat", 150.f}, {"Description", 320.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    // Lot 16 : le plan range par liaison, sous "Plan de".
    auto planPage = std::make_unique<PlanPage>(base + ".planPage");
    planFilterBox_ = &static_cast<ui::DropDown&>(planPage->addChild(std::make_unique<ui::DropDown>(base + ".planFilter")));
    auto plan = std::make_unique<ui::TableView>(base + ".plan");
    plan->setColumns({{"Variable", 250.f, 80.f, true, false}, {"Adresse", 84.f, 50.f, true, false}, {"Type", 150.f, 50.f, true, false},
                      {"Origine", 190.f, 60.f, true, false}, {"\xC3\x89quipement", 150.f, 60.f, true, false}, {"Place Modbus", 190.f, 60.f, true, false},
                      {"Fonctions", 84.f, 40.f, true, false}, {"Qualit\xC3\xA9 (en marche)", 300.f, 60.f, true, false}});
    plan->setSelectionMode(ui::SelectionMode::Single);
    auto report = std::make_unique<ui::TableView>(base + ".report");
    report->setColumns({{"Compte rendu de l'essai", 900.f}});
    auto state = std::make_unique<ui::TableView>(base + ".state");
    state->setColumns({{"Liaison", 310.f}, {"Valeur", 760.f}});
    equipTable_ = &static_cast<ui::TableView&>(equipPage->addChild(std::move(equip)));
    // 1.9 : les reperes des lectures simulees (le poste et Simuler l'IHM), une commande.
    simMarksBox_ = &static_cast<ui::Checkbox&>(equipPage->addChild(
        std::make_unique<ui::Checkbox>("Rep\xC3\xA9rer les lectures simul\xC3\xA9" "es", base + ".simMarks")));
    simMarksBox_->setTooltip("En marche, une valeur lue sur un esclave simul\xC3\xA9 se rep\xC3\xA8re : un cadre violet en tirets et une pastille sur "
                             "l'objet, le bandeau LECTURES SIMUL\xC3\x89" "ES en haut de l'IHM, la barre d'\xC3\xA9tat, les courbes.");
    boundTable_ = bound.get();
    // Lot 17 : la carte memoire.
    auto mapPage = std::make_unique<MapPage>(base + ".mapPage");
    mapTargetBox_ = &static_cast<ui::DropDown&>(mapPage->addChild(std::make_unique<ui::DropDown>(base + ".mapTarget")));
    mapShowBox_ = &static_cast<ui::DropDown&>(mapPage->addChild(std::make_unique<ui::DropDown>(base + ".mapShow")));
    mapShowBox_->setItems({{"tout (activit\xC3\xA9 + mes variables)", "tout", {}, true}, {"mes variables", "variables", {}, true},
                           {"les probl\xC3\xA8mes", "problemes", {}, true}, {"l'activit\xC3\xA9 sans variable", "activite", {}, true}});
    mapShowBox_->setSelectedIndex(0);
    mapRadixBox_ = &static_cast<ui::DropDown&>(mapPage->addChild(std::make_unique<ui::DropDown>(base + ".mapRadix")));
    mapRadixBox_->setItems({{"d\xC3\xA9" "cimal", "decimal", {}, true}, {"sign\xC3\xA9 (INT)", "signe", {}, true}, {"hexad\xC3\xA9" "cimal", "hexa", {}, true}});
    mapRadixBox_->setSelectedIndex(0);
    mapFoldBox_ = &static_cast<ui::Checkbox&>(mapPage->addChild(std::make_unique<ui::Checkbox>("replier le vide", base + ".mapFold")));
    mapFoldBox_->setState(ui::Checkbox::State::Checked);
    mapSearchBox_ = &static_cast<ui::InputText&>(mapPage->addChild(std::make_unique<ui::InputText>(base + ".mapSearch")));
    mapSearchBox_->setPlaceholder("Chercher (variable, 40053, %MW52)");
    map_ = &static_cast<HmiMemoryMap&>(mapPage->addChild(std::make_unique<HmiMemoryMap>(base + ".map")));
    // Lot 18 : les valeurs simulees.
    auto valuesPage = std::make_unique<HmiTwinValues>(base + ".values", false);
    values_ = valuesPage.get();
    valuesCtl_ = std::make_unique<TwinValuesController>(doc_, [this](core::CommandPtr c) { apply_(std::move(c)); });
    valuesCtl_->setHost([this] { return host(); });
    valuesCtl_->say = [this](const std::string& t, bool error) { say(t, error); };
    valuesCtl_->changed = [this] {
        refreshValuesBadge();
        if (side() == Side::Values) rebuildProperties();
    };
    valuesCtl_->attach(*values_);
    table_ = table.get();
    plan_ = &static_cast<ui::TableView&>(planPage->addChild(std::move(plan)));
    report_ = report.get();
    state_ = state.get();
    tabs->addTab({"R\xC3\xA9seau du PC", ui::Icon::Network}, std::move(diagram));
    tabs->addTab({"Scanner IP", ui::Icon::Search}, std::move(scan));
    tabs->addTab({"\xC3\x89quipements", ui::Icon::Module}, std::move(equipPage));
    tabs->addTab({"Variables li\xC3\xA9" "es", ui::Icon::Variable}, std::move(bound));
    tabs->addTab({"Table des adresses", ui::Icon::LocatedVariable}, std::move(table));
    tabs->addTab({"Plan d'adressage", ui::Icon::Document}, std::move(planPage));
    tabs->addTab({"Carte m\xC3\xA9moire", ui::Icon::LocatedVariable}, std::move(mapPage));
    tabs->addTab({"Valeurs simul\xC3\xA9" "es", ui::Icon::Play}, std::move(valuesPage));
    tabs->addTab({"Essai", ui::Icon::Ok}, std::move(report));
    tabs->addTab({"\xC3\x89tat des liaisons", ui::Icon::Info}, std::move(state));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    checks_ = &static_cast<HmiCheckList&>(addChild(std::make_unique<HmiCheckList>(base + ".checks")));
    applyButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Appliquer", base + ".apply")));
    applyButton_->setStyle(ui::Button::Style::Primary);
    cancelButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // 1.9 : a droite de la barre d'etat, "2 equipements lus en simule" ; la carte
    // violette en tete de la fiche d'un esclave simule lie.
    simReads_ = &static_cast<simmark::Indicator&>(status_->addIndicator(std::make_unique<simmark::Indicator>(base + ".simReads"),
                                                                        ui::StatusBar::Slot::Right));
    slaveCard_ = &static_cast<HmiSlaveCard&>(addChild(std::make_unique<HmiSlaveCard>(base + ".slaveCard")));
    slaveCard_->setVisibility(ui::Visibility::Collapsed);
    links_ += slaveCard_->clicked->connect([this](int action) { runSlaveCard(action); });

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CTest: (void)test(); break;
            case CAdd: (void)addAddress("Nouvelle_variable"); break;
            case CRemove: if (!selectedAddress().empty()) (void)removeAddress(selectedAddress()); break;
            case CPropose: (void)propose(); break;
            // Lot 7 : ou exporter (ExportTarget.hpp) - exports/ par defaut, le bouton ... ailleurs.
            case CExport: if (!askExportTarget("le plan d'adressage (Excel)", "Classeurs Excel|*.xlsx", [this] { (void)exportPlan(); })) (void)exportPlan(); break;
            case COpenVar: {
                std::string path = bound_;
                if (static_cast<int>(tabs_->currentIndex()) == TMap)
                    if (const auto sel = map_->selected()) {
                        const auto at = map_->map().at(sel->table, sel->offset);
                        if (!at.empty()) path = map_->map().vars[at.front()].name;
                    }
                if (static_cast<int>(tabs_->currentIndex()) == TPlan)
                    if (const auto* r = selectedPlanRow(); r && (r->kind == PlanRow::Kind::Ihm || r->kind == PlanRow::Kind::Member)) path = r->name;
                if (hosts_.openVariable && !path.empty()) hosts_.openVariable(path);
                break;
            }
            case CReconnect:
                if ((static_cast<int>(tabs_->currentIndex()) == TEquipments || static_cast<int>(tabs_->currentIndex()) == TPlan) && !equipment_.empty()
                    && equipment_ != kPlcKey) {
                    if (auto* h = host()) h->reconnect(equipment_);
                    say("Reconnecter : la liaison avec " + equipment_ + " se refait.");
                } else if (auto* c = hosts_.comm ? hosts_.comm() : nullptr; c && c->link()) {
                    c->link()->reconnect();
                    say("Reconnecter : la liaison se refait.");
                }
                break;
            case CMute: case CUnmute:
                if (auto* c = hosts_.comm ? hosts_.comm() : nullptr) {
                    c->setDemoMute(a == CMute);
                    say(a == CMute ? "Serveur de d\xC3\xA9monstration coup\xC3\xA9 : l'IHM ne re\xC3\xA7oit plus de r\xC3\xA9ponse."
                                   : "Serveur de d\xC3\xA9monstration r\xC3\xA9tabli.");
                }
                break;
            case CRefreshNet: refreshNetwork(); break;
            case CTestAll: testAll(); break;
            case CAddEquip: (void)addEquipment(); break;
            case CRemoveEquip:
                if (!equipment_.empty() && equipment_ != kPlcKey && !askDeleteEquipment(equipment_) && !hosts_.ask) (void)deleteEquipment(equipment_);
                break;
            // Lot 17.
            case CAddVirtual: (void)addVirtualSlave(); break;
            case CCreateTwin: if (!equipment_.empty()) (void)createTwin(equipment_); break;
            case CDetect:
                if (static_cast<int>(tabs_->currentIndex()) == TMap) (void)detectZones(mapEquip_, mapTwin_);
                else if (!equipment_.empty()) (void)detectZones(equipment_, false);
                break;
            case CTwinsStart: runTwins(true); break;
            case CTwinsStop: runTwins(false); break;
            case COpenMap: {
                std::string target = equipment_;
                if (static_cast<int>(tabs_->currentIndex()) == TBound && !bound_.empty())
                    if (const auto* v = doc_->project.variable(bound_)) target = v->equipment;
                // Lot 18 : la carte du jumeau de la ligne choisie, sur sa case.
                std::string cell;
                if (static_cast<int>(tabs_->currentIndex()) == TValues) {
                    target = valuesCtl_->selectedEquipment();
                    cell = valuesCtl_->selectedAddress();
                    if (target.empty()) break;
                    showMap(target, true);
                    tabs_->setCurrentIndex(TMap);
                    if (const auto r = valuesCtl_->row(target, cell)) selectCell(r->table, r->offset);
                    break;
                }
                if (target.empty()) break;
                bool twin = false;
                if (const auto* e = equipmentOf(target); e && host()) twin = host()->viaTwin(e->name) || e->simulated;
                showMap(target, twin);
                tabs_->setCurrentIndex(TMap);
                break;
            }
            case CMapScan: (void)startMapScan(); break;
            case CMapStop: stopMapScan(); break;
            case CMapCreate: (void)createVariableHere(); break;
            case CMapPropose: {
                const auto sel = map_->selected();
                if (!sel) break;
                const auto at = map_->map().at(sel->table, sel->offset);
                if (!at.empty()) (void)proposeFreeAddress(map_->map().vars[at.front()].root);
                break;
            }
            case CMapExport: if (!askExportTarget("la carte m\xC3\xA9moire (Excel)", "Classeurs Excel|*.xlsx", [this] { (void)exportMap(); })) (void)exportMap(); break;
            case CTwinRestart: (void)restartTwin(mapEquip_); break;
            case CTwinSave: (void)saveTwinMemory(mapEquip_); break;
            case CTwinExport:
                if (!askExportTarget("la m\xC3\xA9moire de l'esclave simul\xC3\xA9 (CSV)", "Fichiers CSV|*.csv", [this, e = mapEquip_] { (void)exportTwinMemory(e); }))
                    (void)exportTwinMemory(mapEquip_);
                break;
            case CTwinImport: if (hosts_.askImportTwin) hosts_.askImportTwin(mapEquip_); break;
            case CValAnimateAll: (void)valuesCtl_->animateAll(true); break;
            case CValStopAll: (void)valuesCtl_->animateAll(false); break;
            case CValUnforce: (void)valuesCtl_->unforceAll(); break;
            case CValAdd: {
                std::string eq = valuesCtl_->selectedEquipment();
                if (eq.empty())
                    for (const auto& e : doc_->project.equipments)
                        if (e.hasTwin()) { eq = e.name; break; }
                if (eq.empty()) say("Aucun esclave simul\xC3\xA9 : clone d'abord un vrai appareil (sa fiche : Cloner en esclave simul\xC3\xA9).", true);
                else (void)valuesCtl_->addRegister(eq);
                break;
            }
            case CSimPortAdd: (void)addSimPort(); break;
            case CSimCopy: (void)copyPcNetwork(); break;
            case CSimPortRemove: if (!simPort_.empty()) (void)removeSimPort(simPort_); break;
            case CTestEquip: if (!equipment_.empty()) (void)testEquipment(equipment_); break;
            case CBind: {
                std::string eq = equipment_;
                if (eq.empty() || eq == kPlcKey) {
                    for (const auto& e : doc_->project.equipments)
                        if (e.modbus()) { eq = e.name; break; }
                }
                if (hosts_.askBind) {
                    hosts_.askBind(eq);
                    break;
                }
                std::string name = "Nouvelle_mesure";
                for (int k = 2; doc_->project.variable(name); ++k) name = "Nouvelle_mesure_" + std::to_string(k);
                (void)bindVariable(name, eq);
                break;
            }
            case CUnbind: if (!bound_.empty()) (void)unbindVariable(bound_); break;
            case CPing: case CTool: {
                std::string h, target = equipment_;
                int port = 502, unit = 1;
                if (static_cast<int>(tabs_->currentIndex()) == TScanner) {
                    h = selectedScanned();
                } else if (static_cast<int>(tabs_->currentIndex()) == TBound && !bound_.empty()) {
                    if (const auto* v = doc_->project.variable(bound_)) target = v->equipment;
                }
                if (h.empty() && target == kPlcKey) {
                    h = doc_->project.comm.host;
                    port = doc_->project.comm.port;
                    unit = doc_->project.comm.unit;
                } else if (h.empty()) {
                    if (static_cast<int>(tabs_->currentIndex()) == TMap && !mapEquip_.empty() && mapEquip_ != kPlcKey) target = mapEquip_;
                    if (static_cast<int>(tabs_->currentIndex()) == TValues && !valuesCtl_->selectedEquipment().empty()) target = valuesCtl_->selectedEquipment();
                    if (const auto* e = doc_->project.equipmentByName(target)) {
                        h = e->host;
                        port = e->port;
                        unit = e->unit;
                        // Lot 17 : son jumeau (seulement simule ; celui que montre la carte ; en simulation reseau).
                        const int tab1 = static_cast<int>(tabs_->currentIndex());
                        const bool twin = e->simulated || tab1 == TValues || (tab1 == TMap ? mapTwin_ : (simView_ && e->hasTwin()));
                        if (twin && e->modbus() && host() && host()->simulatedPort(e->name)) {
                            h = "127.0.0.1";
                            port = host()->simulatedPort(e->name);
                        }
                    }
                }
                if (hosts_.openTool) hosts_.openTool(h, port, unit, a == CPing ? "ping" : "lecture");
                break;
            }
            case CRestore: (void)restorePort(); break;
            case CScanStart: (void)startScan(); break;
            case CScanStop: stopScan(); break;
            case CScanAdd: if (!selectedScanned().empty()) (void)addScanned(selectedScanned()); break;
            case CScanExport: if (!askExportTarget("le r\xC3\xA9sultat du scan (CSV)", "Fichiers CSV|*.csv", [this] { (void)exportScan(); })) (void)exportScan(); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += equipTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (refreshing_ || rows.empty() || rows.front() >= equipRows_.size()) return;
        // 1.9 : la ligne d'un esclave simule lie - sa fiche a lui (la carte violette).
        equipment_ = equipRows_[rows.front()].equipment;
        slaveRow_ = equipRows_[rows.front()].slave;
        if (const auto* e = slaveRow_ ? equipmentOf(equipment_) : nullptr)
            say(e->twinLabel() + " : li\xC3\xA9 au vrai appareil ; sa configuration le suit.");
        rebuildProperties();
    });
    links_ += boundTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (refreshing_ || rows.empty() || rows.front() >= boundOrder_.size()) return;
        bound_ = boundOrder_[rows.front()];
        rebuildProperties();
    });
    links_ += scanPage_->table().selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    // Lot 16 : le plan d'adressage - la ligne choisie donne l'equipement (et la
    // variable) a la barre ; un double-clic ouvre la variable IHM ; la fleche replie.
    links_ += plan_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (refreshing_) return;
        if (const auto* r = selectedPlanRow()) {
            equipment_ = r->group;
            if (r->kind == PlanRow::Kind::Ihm || r->kind == PlanRow::Kind::Member) bound_ = r->root;
        }
        syncPlanTools();
        rebuildProperties();
    });
    links_ += plan_->activated->connect([this](ui::RowIndex row) {
        if (row >= planRows_.size()) return;
        const auto& r = planRows_[row];
        if (r.kind == PlanRow::Kind::Group) setPlanGroupOpen(r.group, planClosed_.count(upperOf(r.group)) != 0);
        else if ((r.kind == PlanRow::Kind::Ihm || r.kind == PlanRow::Kind::Member) && hosts_.openVariable) hosts_.openVariable(r.name);
    });
    links_ += plan_->expanderClicked->connect([this](ui::RowIndex row) {
        if (row >= planRows_.size()) return;
        const auto& r = planRows_[row];
        if (r.kind == PlanRow::Kind::Group) setPlanGroupOpen(r.group, planClosed_.count(upperOf(r.group)) != 0);
        else if (r.kind == PlanRow::Kind::Ihm) setPlanVariableOpen(r.name, planOpen_.count(upperOf(r.name)) == 0);
    });
    links_ += planFilterBox_->selectionChanged->connect([this](int) {
        if (refreshing_) return;
        const auto* item = planFilterBox_->selectedItem();
        planFilter_ = item ? item->value : std::string{};
        refreshPlan();
    });
    // Lot 17 : la carte memoire.
    links_ += mapTargetBox_->selectionChanged->connect([this](int i) {
        if (refreshing_ || i < 0 || static_cast<std::size_t>(i) >= mapTargets_.size()) return;
        showMap(mapTargets_[static_cast<std::size_t>(i)].first, mapTargets_[static_cast<std::size_t>(i)].second);
    });
    links_ += mapShowBox_->selectionChanged->connect([this](int i) {
        if (refreshing_) return;
        static const char* k[] = {"tout", "variables", "problemes", "activite sans variable"};
        if (i >= 0 && i < 4) (void)setMapOption("montrer", k[i]);
    });
    links_ += mapRadixBox_->selectionChanged->connect([this](int i) {
        if (refreshing_) return;
        static const char* k[] = {"decimal", "signe", "hexa"};
        if (i >= 0 && i < 3) (void)setMapOption("valeurs", k[i]);
    });
    links_ += mapFoldBox_->stateChanged->connect([this](ui::Checkbox::State st) {
        auto o = map_->options();
        o.fold = st == ui::Checkbox::State::Checked;
        map_->setOptions(o);
    });
    links_ += mapSearchBox_->textChanged->connect([this](const std::string& t) {
        auto o = map_->options();
        o.search = t;
        map_->setOptions(o);
    });
    links_ += map_->cellChosen->connect([this](int, std::uint32_t) { rebuildProperties(); });
    links_ += map_->variableOpened->connect([this](const std::string& name) {
        if (hosts_.openVariable) hosts_.openVariable(name);
    });
    links_ += map_->variableMoved->connect([this](const std::string& root, int, int delta) { (void)moveVariable(root, delta); });
    links_ += equipFilterBox_->selectionChanged->connect([this](int) {
        if (refreshing_) return;
        const auto* item = equipFilterBox_->selectedItem();
        setEquipmentFilter(item ? item->value : std::string("tous"));
    });
    links_ += simMarksBox_->stateChanged->connect([this](ui::Checkbox::State st) {
        if (refreshing_) return;
        (void)setSimMarks(st == ui::Checkbox::State::Checked);
    });
    links_ += diagram_->equipmentDropped->connect([this](const std::string& dropped, const std::string& port) {
        if (!askMoveToPort(dropped, port) && !hosts_.ask) (void)moveToPort(dropped, port, true);
    });
    links_ += diagram_->deleteRequested->connect([this] {
        if (!portSide_ && !equipment_.empty()) (void)askDeleteEquipment(equipment_);
        else if (simView_ && portSide_ && !simPort_.empty()) (void)removeSimPort(simPort_);
    });
    links_ += diagram_->viewToggled->connect([this](bool simulated) { setNetworkView(simulated); });
    links_ += tabs_->currentChanged->connect([this](std::size_t) {
        syncPlanTools();
        invalidateLayout();
        rebuildProperties();
        onLayout();
    });
    links_ += diagram_->portClicked->connect([this](const std::string& key) {
        if (simView_) selectSimPort(key);
        else selectPort(key);
    });
    links_ += diagram_->equipmentClicked->connect([this](const std::string& key) {
        equipment_ = key;
        portSide_ = false;
        refreshDiagram();
        rebuildProperties();
        onLayout();
    });
    links_ += diagram_->giveClicked->connect([this](int i) {
        // Lot 17 : dans le reseau simule, "Ajouter au PC simule un port...".
        if (simView_) {
            if (i >= 0 && static_cast<std::size_t>(i) < gives_.size() && !gives_[static_cast<std::size_t>(i)].ip.empty())
                (void)addSimPort(gives_[static_cast<std::size_t>(i)].ip, gives_[static_cast<std::size_t>(i)].prefix);
            return;
        }
        (void)giveOutside(i);
    });
    links_ += diagram_->editClicked->connect([this](int i) {
        const auto& out = diagram_->data().outside;
        if (i < 0 || static_cast<std::size_t>(i) >= out.size()) return;
        selectEquipment(out[static_cast<std::size_t>(i)].equip);
        tabs_->setCurrentIndex(TEquipments);
        (void)grid_->revealValue("Adresse IP");
        say("Changez l'adresse IP de " + out[static_cast<std::size_t>(i)].equip + " (\xC3\xA0 droite) : celle qu'il a vraiment, dans un r\xC3\xA9seau du PC.");
    });
    links_ += applyButton_->clicked->connect([this] { (void)applyPort(); });
    links_ += cancelButton_->clicked->connect([this] { cancelPort(); });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

HmiCommPane::~HmiCommPane() {
    if (scanner_) scanner_->stop();
    if (mapScanner_) mapScanner_->stop();
    if (detector_) detector_->stop();
}

void HmiCommPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiCommPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

hmi::comm::Plan HmiCommPane::currentPlan() const {
    return CommHost::planFor(doc_->project.comm, hosts_.plc ? hosts_.plc() : nullptr, hosts_.runtime ? hosts_.runtime() : nullptr);
}

std::string HmiCommPane::selectedAddress() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? std::string{} : order_[rows.front()];
}

void HmiCommPane::selectAddress(const std::string& variable) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], variable)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

void HmiCommPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedAddress();
    const auto& comm = doc_->project.comm;
    const auto plan = currentPlan();
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (const auto& a : comm.addresses) {
        order_.push_back(a.variable);
        std::string place, functions, state, type = a.type;
        int tone = 1;
        if (const auto p = plan.resolve(a.variable)) {
            place = p->placeText();
            functions = p->functionsText();
            state = "plac\xC3\xA9" "e";
            if (type.empty()) type = std::string(sim::toString(p->type)) + " (programme)";
        } else {
            const auto why = plan.whyNot(a.variable);
            state = why.empty() ? std::string("refus\xC3\xA9" "e") : why;
            tone = 3;
        }
        rows.push_back({a.variable, a.address, type, a.readOnly ? std::string("lecture seule") : std::string("lecture, \xC3\xA9" "criture"), place,
                        functions, state, a.description});
        tones.push_back(tone);
    }
    tableModel_ = std::make_shared<Rows>(std::vector<std::string>{"Variable", "Adresse", "Type", "Acc\xC3\xA8s", "Place Modbus", "Fonctions", "\xC3\x89tat",
                                                                  "Description"},
                                         std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r >= tones.size()) return st;
                                             if (c == 0) {
                                                 st.bold = true;
                                                 st.icon = ui::Icon::Variable;
                                                 st.iconTone = toneOf(tones[r]);
                                             }
                                             if (c == 6) st.fgTone = toneOf(tones[r]);
                                             return st;
                                         });
    table_->setModel(tableModel_);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (!keep.empty() && same(order_[i], keep)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    if (selectedAddress().empty() && !order_.empty()) table_->selectModelRows({0});
    tabs_->setTabBadge(TTable, std::to_string(comm.addresses.size()));
    refreshing_ = false;
    refreshPlan();
    refreshState();
    // Lot 15 : les equipements, les variables liees, le schema, le scanner.
    refreshEquipments();
    refreshBound();
    refreshDiagram();
    refreshScan();
    // Lot 17 : la carte memoire suit le projet.
    refreshMapTargets();
    refreshMap(true);
    // Lot 18 : les valeurs simulees aussi.
    if (valuesCtl_) valuesCtl_->refresh();
    refreshValuesBadge();
    rebuildProperties();
    std::size_t unlocated = 0;
    for (const auto& [name, why] : plan.refused()) unlocated += why.rfind("non localis", 0) == 0;
    std::size_t bound = 0;
    for (const auto& v : doc_->project.programs.variables) bound += v.bound() ? 1 : 0;
    status_->setMessage(std::string(comm.modbus() ? "Automate : Modbus TCP vers " + comm.host + ":" + std::to_string(comm.port) : std::string("Automate : simulateur"))
                            + " \xC2\xB7 " + std::to_string(plan.points().size()) + " variable(s) au plan d'adressage \xC2\xB7 "
                            + std::to_string(unlocated) + " non localis\xC3\xA9" "e(s) \xC2\xB7 " + std::to_string(doc_->project.equipments.size())
                            + " \xC3\xA9quipement(s), " + std::to_string(bound) + " variable(s) li\xC3\xA9" "e(s)",
                        ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiCommPane::refreshPlan() {
    // Lot 16 : range par liaison - l'automate du projet, puis chaque equipement Modbus.
    const auto plan = currentPlan();
    auto* host = hosts_.comm ? hosts_.comm() : nullptr;
    const hmi::comm::Link* link = host ? host->link() : nullptr;
    const auto& p = doc_->project;
    const auto& comm = p.comm;
    const std::string keepKey = [&] {
        const auto* r = selectedPlanRow();
        return r ? r->group + "|" + r->name + "|" + std::to_string(static_cast<int>(r->kind)) : std::string{};
    }();
    planNames_.clear();
    planRows_.clear();
    std::vector<PlanModel::Line> lines;
    const auto qualityText = [](hmi::comm::Quality q, const std::string& why, int& tone) {
        tone = q == hmi::comm::Quality::Good ? 1 : q == hmi::comm::Quality::Stale ? 2 : q == hmi::comm::Quality::Bad ? 3 : 0;
        if (q == hmi::comm::Quality::Pending) return std::string("pas lue (aucune vue ne la montre)");
        return std::string(hmi::comm::qualityName(q)) + (why.empty() ? std::string{} : " : " + why);
    };
    const auto shown = [&](const std::string& group) { return planFilter_.empty() || same(planFilter_, group); };
    // Le filtre "Plan de" : tous, l'automate, chaque equipement Modbus.
    std::size_t modbusEquipments = 0;
    for (const auto& e : p.equipments) modbusEquipments += e.modbus() ? 1 : 0;
    {
        std::vector<ui::DropDown::Item> items{
            {"Tous : l'automate du projet + " + std::to_string(modbusEquipments) + " \xC3\xA9quipement" + (modbusEquipments > 1 ? "s" : ""), "", {}, true},
            {"L'automate du projet", kPlcKey, {}, true}};
        int selected = 0;
        if (planFilter_ == kPlcKey) selected = 1;
        for (const auto& e : p.equipments) {
            if (!e.modbus()) continue;
            items.push_back({"\xC3\x89quipement \xC2\xAB " + e.name + " \xC2\xBB", e.name, {}, true});
            if (same(planFilter_, e.name)) selected = static_cast<int>(items.size()) - 1;
        }
        if (selected == 0 && !planFilter_.empty()) planFilter_.clear();
        const bool was = refreshing_;
        refreshing_ = true;
        planFilterBox_->setItems(std::move(items));
        planFilterBox_->setSelectedIndex(selected);
        refreshing_ = was;
    }
    // ---- l'automate du projet
    std::size_t fromProgram = 0, fromTable = 0, unlocated = 0;
    for (const auto& pt : plan.points()) (pt.origin == "table" ? fromTable : fromProgram) += 1;
    for (const auto& [name, why] : plan.refused()) unlocated += why.rfind("non localis", 0) == 0 ? 1 : 0;
    if (shown(kPlcKey)) {
        const bool open = !planClosed_.count(upperOf(kPlcKey));
        std::string state = comm.modbus() ? (link ? (link->diagnostics().connected ? std::string("connect\xC3\xA9") : std::string("pas de liaison")) : std::string("pas de liaison"))
                                          : std::string("simulateur de l'application");
        PlanModel::Line g;
        g.kind = 0;
        g.expander = open ? 1 : 0;
        g.tone = state == "connect\xC3\xA9" ? 1 : comm.modbus() ? 3 : 4;
        g.cells = {"AUTOMATE DU PROJET   " + (comm.modbus() ? "Modbus TCP \xC2\xB7 " + comm.host + ":" + std::to_string(comm.port) + " \xC2\xB7 esclave " + std::to_string(comm.unit)
                                                           : std::string("le simulateur"))
                       + "  \xE2\x80\x94  " + std::to_string(plan.points().size()) + " variables de l'automate (programme " + std::to_string(fromProgram)
                       + ", table des adresses " + std::to_string(fromTable) + ")  \xC2\xB7  " + state,
                   "plc"};
        g.tip = "Les variables de l'automate du projet que l'IHM lit par Modbus, \xC3\xA0 leur adresse (le programme, ou la table des adresses).";
        lines.push_back(std::move(g));
        planRows_.push_back({PlanRow::Kind::Group, kPlcKey, "AUTOMATE DU PROJET", {}, {}});
        planNames_.push_back({});
        if (open) {
            for (const auto& pt : plan.points()) {
                std::string quality = "\xE2\x80\x94";
                int tone = 0;
                if (link) {
                    std::string why;
                    const std::string probe = pt.array ? pt.name + "[" + std::to_string(pt.low) + "]" : pt.name;
                    quality = qualityText(link->quality(probe, &why), why, tone);
                }
                PlanModel::Line l;
                l.kind = 1;
                l.indent = 12.f;
                l.tone = tone;
                l.cells = {pt.name, pt.address, pt.typeName, "Automate \xC2\xB7 " + pt.origin, "Automate du projet", pt.placeText(), pt.functionsText(), quality};
                l.tip = pt.name + " \xE2\x80\x94 variable de l'automate du projet (" + pt.origin + "), " + pt.typeName + ", " + pt.modbusText() + ".";
                lines.push_back(std::move(l));
                planRows_.push_back({PlanRow::Kind::Plc, kPlcKey, pt.name, {}, {}});
                planNames_.push_back(pt.name);
            }
            // Les refusees - sauf les non localisees (le programme en a des milliers) : leur nombre.
            for (const auto& [name, why] : plan.refused()) {
                if (why.rfind("non localis", 0) == 0) continue;
                PlanModel::Line l;
                l.kind = 4;
                l.indent = 12.f;
                l.tone = 3;
                l.cells = {name, "\xE2\x80\x94", "\xE2\x80\x94", "refus\xC3\xA9" "e", "Automate du projet", why, "", "\xE2\x80\x94"};
                l.tip = name + " : " + why;
                lines.push_back(std::move(l));
                planRows_.push_back({PlanRow::Kind::Refused, kPlcKey, name, {}, {}});
                planNames_.push_back(name);
            }
        }
    }
    // ---- chaque equipement Modbus : ses variables IHM
    std::size_t equipPoints = 0;
    const auto* eh = this->host();
    for (const auto& e : p.equipments) {
        if (!e.modbus()) continue;
        const auto ep = hmi::equip::buildPlan(p, e);
        equipPoints += ep.points().size();
        if (!shown(e.name)) continue;
        hmi::comm::Link* lk = eh ? eh->link(e.name) : nullptr;
        const auto bound = hmi::equip::boundVariables(p, e);
        // Lot 17 : la regle de la carte memoire - une ecriture et une lecture sur
        // les memes cases : attention ; deux ecritures : erreur ; hors zone : erreur.
        const auto memory = hmi::zones::buildMap(p, e);
        const auto clash = [&](const std::string& name, int& tone) {
            std::string text;
            for (const auto& c : memory.conflicts) {
                if (c.severity < 2) continue;
                const auto& a = memory.vars[c.a];
                const auto& b = memory.vars[c.b];
                if (!same(a.name, name) && !same(b.name, name) && !same(a.root, name) && !same(b.root, name)) continue;
                tone = std::max(tone, c.severity);
                if (text.empty()) text = hmi::zones::conflictText(memory, c);
            }
            for (const auto& v : memory.vars)
                if (v.outOfZone && (same(v.name, name) || same(v.root, name))) {
                    tone = 3;
                    if (text.empty()) text = v.name + " : hors des zones de l'\xC3\xA9quipement (il la refusera)";
                }
            return text;
        };
        // Les mots distincts : des BOOL ranges 16 par mot (lot 16) ne comptent qu'un mot.
        std::set<std::pair<int, long long>> usedWords;
        for (const auto& pt : ep.points())
            if (!pt.bits())
                for (long long k = 0; k < static_cast<long long>(std::max<std::size_t>(pt.span(), 1)); ++k)
                    usedWords.insert({static_cast<int>(pt.area), static_cast<long long>(pt.offset) + k});
        const long long words = static_cast<long long>(usedWords.size());
        std::string state = !e.enabled ? std::string("d\xC3\xA9sactiv\xC3\xA9") : e.simulated ? std::string("simul\xC3\xA9") : std::string("pas encore test\xC3\xA9");
        int stateTone = e.simulated ? 4 : 0;
        if (eh)
            for (const auto& st : eh->statuses())
                if (same(st.name, e.name)) {
                    state = st.state;
                    stateTone = !st.enabled ? 0 : st.simulated ? 4 : !st.tested ? 0 : st.reachable ? 1 : 3;
                }
        std::string lowered = state;
        if (!lowered.empty()) lowered[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[0])));
        const bool open = !planClosed_.count(upperOf(e.name));
        const std::string address = (e.simulated ? std::string("simul\xC3\xA9") : e.host + ":" + std::to_string(e.port)) + " \xC2\xB7 esclave " + std::to_string(e.unit);
        PlanModel::Line g;
        g.kind = 0;
        g.expander = open ? 1 : 0;
        g.tone = stateTone;
        g.cells = {"\xC3\x89QUIPEMENT \xC2\xAB " + e.name + " \xC2\xBB   Modbus TCP/IP \xC2\xB7 " + address + "  \xE2\x80\x94  " + std::to_string(bound.size())
                       + " variable" + (bound.size() > 1 ? "s" : "") + " IHM li\xC3\xA9" "e" + (bound.size() > 1 ? "s" : "") + " (" + std::to_string(ep.points().size())
                       + " case" + (ep.points().size() > 1 ? "s" : "") + ", " + std::to_string(words) + " mot" + (words > 1 ? "s" : "") + ")  \xC2\xB7  " + lowered,
                   "eq"};
        g.tip = "Les variables IHM que l'IHM lit et \xC3\xA9" "crit dans l'\xC3\xA9quipement \xC2\xAB " + e.name + " \xC2\xBB (Configuration \xE2\x80\xBA \xC3\x89quipements). "
                "Ce ne sont pas des variables de l'automate du projet.";
        lines.push_back(std::move(g));
        planRows_.push_back({PlanRow::Kind::Group, e.name, e.name, {}, {}});
        planNames_.push_back({});
        if (!open) continue;
        const auto leafQuality = [&](const std::string& name, int& tone) {
            tone = 0;
            if (!lk) return std::string("\xE2\x80\x94");
            std::string why;
            return qualityText(lk->quality(name, &why), why, tone);
        };
        for (const auto* v : bound) {
            const bool composite = hmi::types::isComposite(v->type);
            const std::string folder = v->folder.empty() ? std::string("racine") : v->folder;
            const std::string origin = "Variable IHM \xC2\xB7 " + (v->folder.empty() ? std::string("racine") : hmi::types::folderLeaf(v->folder));
            const std::string where = "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM" + (v->folder.empty() ? std::string{} : " \xE2\x80\xBA " + v->folder);
            const std::string linkText = "Modbus TCP/IP " + address;
            if (!composite) {
                const auto pt = ep.resolve(v->name);
                int tone = 0;
                std::string quality = leafQuality(v->name, tone);
                std::string reason;
                if (!pt) {
                    reason = ep.whyNot(v->name);
                    tone = 3;
                    quality = "refus\xC3\xA9" "e : " + reason;
                }
                PlanModel::Line l;
                l.kind = 2;
                l.indent = 12.f;
                l.tone = tone;
                l.cells = {v->name, v->address, v->type, origin, e.name, pt ? pt->placeText() + " \xC2\xB7 " + hmi::equip::modiconText(*pt) : reason,
                           pt ? pt->functionsText() : std::string{}, quality};
                l.tip = v->name + " \xE2\x80\x94 variable IHM (" + where + "), " + v->type + ".\nElle est lue" + (v->readOnly ? std::string{} : std::string(" et \xC3\xA9" "crite"))
                      + " sur l'\xC3\xA9quipement \xC2\xAB " + e.name + " \xC2\xBB (" + linkText + ")" + (pt ? ", " + hmi::equip::modiconText(*pt) : std::string{}) + ".\n"
                      + (tone == 3 ? quality + "\n" : std::string{}) + "Ce n'est pas une variable de l'automate du projet. Double-clic : l'ouvrir dans Variables IHM.";
                {
                    int clashTone = 0;
                    const std::string c = clash(v->name, clashTone);
                    if (!c.empty()) {
                        l.tone = std::max(l.tone, clashTone);
                        l.cells[7] = (clashTone == 3 ? "erreur : " : "attention : ") + c;
                        l.tip += "\n" + c + " (Carte m\xC3\xA9moire).";
                    }
                }
                lines.push_back(std::move(l));
                planRows_.push_back({PlanRow::Kind::Ihm, e.name, v->name, v->name, {}});
                planNames_.push_back(v->name);
                continue;
            }
            // Une structure ou un tableau : sa ligne, puis ses cases (depliee).
            const bool vopen = planOpen_.count(upperOf(v->name)) != 0;
            const auto leaves = hmi::types::leafVariables(p, *v);
            int worst = 1;
            std::string worstText;
            bool anyRead = false;
            for (const auto& lf : leaves) {
                int tone = 0;
                const std::string q = leafQuality(lf.name, tone);
                if (tone != 0) anyRead = true;
                if (tone > worst || (tone == 3 && worst != 3)) {
                    worst = tone;
                    worstText = lf.name + " : " + q;
                }
            }
            std::string quality = !lk ? std::string("\xE2\x80\x94") : !anyRead ? std::string("pas lue (aucune vue ne la montre)") : worst == 1 ? std::string("bonne") : worstText;
            PlanModel::Line l;
            l.kind = 2;
            l.indent = 12.f;
            l.expander = vopen ? 1 : 0;
            l.tone = lk && anyRead ? worst : 0;
            l.cells = {v->name, v->address, v->type, origin, e.name, hmi::types::spanText(p, *v), "", quality};
            l.tip = v->name + " \xE2\x80\x94 variable IHM (" + where + "), " + hmi::types::summary(p, v->type, v->packBools) + ".\nSes " + std::to_string(leaves.size())
                  + " cases sont lues" + (v->readOnly ? std::string{} : std::string(" et \xC3\xA9" "crites")) + " sur l'\xC3\xA9quipement \xC2\xAB " + e.name + " \xC2\xBB ("
                  + linkText + "), " + hmi::types::spanText(p, *v) + ".\nCe n'est pas une variable de l'automate du projet. Double-clic : l'ouvrir dans Variables IHM.";
            lines.push_back(std::move(l));
            planRows_.push_back({PlanRow::Kind::Ihm, e.name, v->name, v->name, {}});
            planNames_.push_back(v->name);
            if (!vopen) continue;
            std::size_t n = 0;
            for (const auto& lf : leaves) {
                if (++n > 400) break;
                const auto pt = ep.resolve(lf.name);
                int tone = 0;
                std::string q = leafQuality(lf.name, tone);
                if (!pt) {
                    q = "refus\xC3\xA9" "e : " + ep.whyNot(lf.name);
                    tone = 3;
                }
                bool fixed = false;
                for (const auto& pl : v->places)
                    if (upperOf(v->name + (pl.path.empty() || pl.path.front() == '[' ? "" : ".") + pl.path) == upperOf(lf.name)) fixed = true;
                PlanModel::Line m;
                m.kind = 3;
                m.indent = 44.f;
                m.tone = tone;
                m.cells = {lf.name, lf.address, lf.type, "membre de " + v->name + (fixed ? " \xE2\x9C\x8E" : ""), e.name,
                           pt ? pt->placeText() + " \xC2\xB7 " + hmi::equip::modiconText(*pt) : std::string{}, pt ? pt->functionsText() : std::string{}, q};
                m.tip = lf.name + " \xE2\x80\x94 un membre de la variable IHM " + v->name + " (" + lf.type + "), " + (pt ? hmi::equip::modiconText(*pt) : q)
                      + (fixed ? " (adresse corrig\xC3\xA9" "e \xC3\xA0 la main)" : std::string(" (sa place dans la structure)")) + ", sur \xC2\xAB " + e.name + " \xC2\xBB.";
                lines.push_back(std::move(m));
                planRows_.push_back({PlanRow::Kind::Member, e.name, lf.name, v->name, {}});
                planNames_.push_back(lf.name);
            }
        }
        for (const auto& [name, why] : ep.refused()) {
            bool listed = false;
            for (const auto& r : planRows_) listed = listed || (r.kind != PlanRow::Kind::Group && same(r.name, name) && same(r.group, e.name));
            if (listed) continue;
            PlanModel::Line l;
            l.kind = 4;
            l.indent = 12.f;
            l.tone = 3;
            l.cells = {name, "\xE2\x80\x94", "\xE2\x80\x94", "refus\xC3\xA9" "e", e.name, why, "", "\xE2\x80\x94"};
            l.tip = name + " : " + why;
            lines.push_back(std::move(l));
            planRows_.push_back({PlanRow::Kind::Refused, e.name, name, {}, {}});
            planNames_.push_back(name);
        }
    }
    for (std::size_t i = 0; i < planRows_.size() && i < lines.size(); ++i) planRows_[i].tip = lines[i].tip;
    planModel_ = std::make_shared<PlanModel>(std::move(lines));
    const bool was = refreshing_;
    refreshing_ = true;
    plan_->setModel(planModel_);
    for (std::size_t i = 0; i < planRows_.size(); ++i) {
        const auto& r = planRows_[i];
        if (r.group + "|" + r.name + "|" + std::to_string(static_cast<int>(r.kind)) == keepKey) plan_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    }
    refreshing_ = was;
    const std::size_t ihm = [&] {
        std::size_t n = 0;
        for (const auto& v : p.programs.variables) n += v.bound() ? 1 : 0;
        return n;
    }();
    tabs_->setTabBadge(TPlan, std::to_string(plan.points().size()) + (ihm ? " + " + std::to_string(ihm) + " IHM" : std::string{}), ui::Tone::Accent);
    (void)unlocated;
    (void)equipPoints;
    syncPlanTools();
}

const HmiCommPane::PlanRow* HmiCommPane::selectedPlanRow() const {
    const auto sel = plan_ ? plan_->selectedModelRows() : std::vector<ui::RowIndex>{};
    return !sel.empty() && sel.front() < planRows_.size() ? &planRows_[sel.front()] : nullptr;
}

void HmiCommPane::setPlanFilter(const std::string& group) {
    planFilter_ = group;
    refreshPlan();
}

void HmiCommPane::setPlanGroupOpen(const std::string& group, bool open) {
    if (open) planClosed_.erase(upperOf(group));
    else planClosed_.insert(upperOf(group));
    refreshPlan();
}

void HmiCommPane::setPlanVariableOpen(const std::string& variable, bool open) {
    if (open) planOpen_.insert(upperOf(variable));
    else planOpen_.erase(upperOf(variable));
    refreshPlan();
}

// La barre du plan : le bouton Tester (et Reconnecter) dit quel equipement.
void HmiCommPane::syncPlanTools() {
    if (!tools_) return;
    const auto* r = static_cast<int>(tabs_ ? tabs_->currentIndex() : 0) == TPlan ? selectedPlanRow() : nullptr;
    if (r && r->group != kPlcKey) {
        tools_->setText(CTestEquip, "Tester l'\xC3\xA9quipement \xC2\xAB " + r->group + " \xC2\xBB : connexion, identification, lecture de ses variables",
                        "Tester \xC2\xAB " + r->group + " \xC2\xBB");
        tools_->setText(CReconnect, "Reconnecter : la liaison avec \xC2\xAB " + r->group + " \xC2\xBB se refait tout de suite", "Reconnecter \xC2\xAB " + r->group + " \xC2\xBB");
        std::string ip;
        if (const auto* e = doc_->project.equipmentByName(r->group)) ip = e->simulated ? std::string("127.0.0.1") : e->host;
        tools_->setText(CPing, "Ping : l'outil Modbus, onglet Ping, vers l'\xC3\xA9quipement choisi", ip.empty() ? std::string("Ping\xE2\x80\xA6") : "Ping " + ip);
    } else {
        tools_->setText(CTestEquip, "Tester l'\xC3\xA9quipement choisi : connexion, identification, lecture de ses variables (ou ping et port)", "Tester");
        tools_->setText(CReconnect, "Reconnecter : la liaison se refait tout de suite", "Reconnecter");
        tools_->setText(CPing, "Ping : l'outil Modbus, onglet Ping, vers l'\xC3\xA9quipement choisi", "Ping\xE2\x80\xA6");
    }
    tools_->invalidate();
}

void HmiCommPane::refreshState() {
    const auto& comm = doc_->project.comm;
    auto* host = hosts_.comm ? hosts_.comm() : nullptr;
    const hmi::comm::Link* link = host ? host->link() : nullptr;
    const bool simulator = hosts_.runtime && hosts_.runtime();
    const auto rowsOf = hmi::comm::diagnosticRows(comm, link, simulator, host && host->running(), host ? host->demoPort() : 0);
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    // Lot 15 : l'automate d'abord (son titre), puis chaque equipement.
    rows.push_back({"AUTOMATE DU PROJET", comm.modbus() ? comm.host + ":" + std::to_string(comm.port) : std::string("simulateur de l'application")});
    tones.push_back(4);
    for (const auto& r : rowsOf) {
        rows.push_back({r.label, r.value});
        tones.push_back(r.tone);
    }
    if (host && !host->demoError().empty()) {
        rows.push_back({"Serveur de d\xC3\xA9monstration", "impossible : " + host->demoError()});
        tones.push_back(3);
    } else if (host && host->running()) {
        const auto st = host->demoStats();
        rows.push_back({"Clients du serveur", std::to_string(st.clients) + " connect\xC3\xA9(s) \xC2\xB7 " + std::to_string(st.requests) + " requ\xC3\xAAte(s) servie(s)"
                                                   + (st.lastClient.empty() ? std::string{} : " \xC2\xB7 dernier : " + st.lastClient)
                                                   + (host->demoMute() ? " \xC2\xB7 COUP\xC3\x89 (ne r\xC3\xA9pond plus)" : std::string{})});
        tones.push_back(host->demoMute() ? 3 : 0);
    }
    if (link)
        for (const auto& [name, why] : link->diagnostics().badPoints) {
            rows.push_back({"Variable en d\xC3\xA9" "faut", name + " \xE2\x80\x94 " + why});
            tones.push_back(why.rfind("ancienne", 0) == 0 ? 2 : 3);
        }
    // Lot 15 : les equipements.
    std::size_t reachable = 0, tested = 0;
    if (auto* eh = this->host()) {
        for (const auto& st : eh->statuses()) {
            if (st.enabled && !st.simulated) {
                ++tested;
                reachable += st.reachable ? 1 : 0;
            }
            rows.push_back({"\xC3\x89QUIPEMENT " + st.name, st.type + " \xC2\xB7 " + st.address});
            tones.push_back(4);
            const int tone = !st.enabled ? 0 : st.simulated ? 4 : !st.tested ? 0 : st.reachable ? 1 : 3;
            rows.push_back({"\xC3\x89tat", st.state + (st.why.empty() ? std::string{} : " \xE2\x80\x94 " + st.why)});
            tones.push_back(tone);
            if (const auto pr = eh->probe(st.name); pr && pr->done) {
                char ms[32];
                if (pr->ms < 0.1) std::snprintf(ms, sizeof ms, "< 0.1 ms");
                else std::snprintf(ms, sizeof ms, "%.1f ms", pr->ms);
                std::string text = pr->ok ? std::string(ms) : "pas de r\xC3\xA9ponse (" + pr->why + ")";
                if (pr->portTried) text += " \xC2\xB7 port : " + std::string(pr->portOk ? "ouvert" : "ferm\xC3\xA9 (" + pr->portWhy + ")");
                rows.push_back({"Ping", text + " \xC2\xB7 " + clockOf(pr->at)});
                tones.push_back(pr->ok || pr->portOk ? 1 : 3);
            }
            if (const auto* lk = eh->link(st.name)) {
                const auto d = lk->diagnostics();
                char resp[96];
                std::snprintf(resp, sizeof resp, "%.1f ms (moyenne %.1f, max %.1f)", d.lastMs, d.avgMs, d.maxMs);
                rows.push_back({"Liaison Modbus", d.state + " \xC2\xB7 " + std::to_string(d.requests) + " requ\xC3\xAAte(s), " + std::to_string(d.errors)
                                                     + " erreur(s), " + std::to_string(d.reconnects) + " reconnexion(s)"});
                tones.push_back(d.connected ? 1 : 3);
                rows.push_back({"Temps de r\xC3\xA9ponse", resp});
                tones.push_back(0);
                rows.push_back({"Variables", std::to_string(d.good) + " bonne(s), " + std::to_string(d.stale) + " ancienne(s), " + std::to_string(d.bad)
                                                 + " mauvaise(s), " + std::to_string(d.pending) + " en attente"});
                tones.push_back(d.bad ? 3 : d.stale ? 2 : 0);
                if (!d.device.empty()) {
                    rows.push_back({"Identification", d.device});
                    tones.push_back(0);
                }
                for (const auto& [name, why] : d.badPoints) {
                    rows.push_back({"Variable en d\xC3\xA9" "faut", name + " \xE2\x80\x94 " + why});
                    tones.push_back(why.rfind("ancienne", 0) == 0 ? 2 : 3);
                }
            }
            if (st.simulated) {
                const auto ss = eh->simulatedStats(st.name);
                rows.push_back({"Serveur simul\xC3\xA9", std::to_string(ss.clients) + " client(s) \xC2\xB7 " + std::to_string(ss.requests) + " requ\xC3\xAAte(s) servie(s)"});
                tones.push_back(4);
            }
        }
    }
    stateModel_ = std::make_shared<Rows>(std::vector<std::string>{"Liaison", "Valeur"}, std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
        ui::CellStyle st;
        if (r >= tones.size()) return st;
        if (c == 0) st.bold = true;
        if (c == 1) st.fgTone = toneOf(tones[r]);
        if (tones[r] == 4 && c == 0) {
            st.icon = ui::Icon::Module;
            st.iconTone = ui::Tone::Accent;
        }
        return st;
    });
    state_->setModel(stateModel_);
    int worst = 0;
    for (const int t : tones) worst = std::max(worst, t == 4 ? 0 : t);
    // Le badge : les liaisons joignables sur celles qu'on attend (l'automate reel compte).
    if (link) {
        ++tested;
        reachable += link->connected() ? 1 : 0;
    }
    tabs_->setTabLive(TState, reachable > 0);
    if (tested == 0)
        tabs_->setTabBadge(TState, "simulateur", ui::Tone::Accent);
    else
        tabs_->setTabBadge(TState, std::to_string(reachable) + " / " + std::to_string(tested) + " joignable" + (reachable > 1 ? "s" : ""),
                           reachable == tested ? (worst >= 2 ? ui::Tone::Warning : ui::Tone::Ok) : reachable == 0 ? ui::Tone::Error : ui::Tone::Warning);
}

bool HmiCommPane::change(const std::string& label, const std::function<void(hmi::Communication&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p.comm); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiCommPane::setSetting(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    const auto number = [&](long long lo, long long hi, long long& out) {
        char* end = nullptr;
        const long long n = std::strtoll(v.c_str(), &end, 10);
        if (v.empty() || (end && *end)) return false;
        if (n < lo || n > hi) return false;
        out = n;
        return true;
    };
    long long n = 0;
    const auto& c = doc_->project.comm;
    if (key == "mode") {
        const std::string l = lower(v);
        const std::string mode = l.find("modbus") != std::string::npos ? "modbus" : l.rfind("sim", 0) == 0 ? "simulateur" : "";
        if (mode.empty()) return fail("mode \xC2\xAB " + v + " \xC2\xBB : Simulateur ou Modbus TCP");
        if (mode == c.mode) return true;
        if (!change(mode == "modbus" ? "Communication : Modbus TCP" : "Communication : simulateur", [&](hmi::Communication& x) { x.mode = mode; }))
            return false;
        say(mode == "modbus" ? "L'IHM lira l'automate " + c.host + ":" + std::to_string(c.port) + " en Modbus TCP (la simulation IHM le montre)."
                             : "L'IHM lit le simulateur de l'application.");
        return true;
    }
    if (key == "hote") {
        if (v.empty() || v.find(' ') != std::string::npos) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : une adresse IP (192.168.1.10) ou un nom");
        return change("Adresse de l'automate", [&](hmi::Communication& x) { x.host = v; });
    }
    struct Num {
        const char* key;
        long long lo, hi;
        int hmi::Communication::*field;
        const char* label;
    };
    static const Num kNums[] = {
        {"port", 1, 65535, &hmi::Communication::port, "Port"},
        {"esclave", 0, 255, &hmi::Communication::unit, "Esclave"},
        {"delai", 50, 60000, &hmi::Communication::timeoutMs, "D\xC3\xA9lai de r\xC3\xA9ponse"},
        {"periode", 50, 600000, &hmi::Communication::periodMs, "P\xC3\xA9riode de scrutation"},
        {"reessai", 1, 3600, &hmi::Communication::retryS, "Nouvel essai"},
        {"mots", 1, 125, &hmi::Communication::maxWords, "Mots par requ\xC3\xAAte"},
        {"bits", 1, 2000, &hmi::Communication::maxBits, "Bits par requ\xC3\xAAte"},
        {"ecart", 0, 100, &hmi::Communication::gap, "Regroupement"},
        {"mauvaise", 1, 86400, &hmi::Communication::badAfterS, "Mauvaise apr\xC3\xA8s"},
        {"demo_port", 1, 65535, &hmi::Communication::demoPort, "Port du serveur de d\xC3\xA9monstration"},
    };
    for (const auto& k : kNums)
        if (key == k.key) {
            if (!number(k.lo, k.hi, n)) return fail(std::string(k.label) + " : un nombre de " + std::to_string(k.lo) + " \xC3\xA0 " + std::to_string(k.hi));
            return change(k.label, [&](hmi::Communication& x) { x.*(k.field) = static_cast<int>(n); });
        }
    if (key == "ordre") {
        const std::string l = lower(v);
        const std::string order = l.find("fort") != std::string::npos ? "fort" : l.find("faible") != std::string::npos ? "faible" : "";
        if (order.empty()) return fail("ordre des mots : poids faible d'abord (Schneider), ou poids fort d'abord");
        return change("Ordre des mots", [&](hmi::Communication& x) { x.wordOrder = order; });
    }
    if (key == "ecritures" || key == "demo" || key == "demo_reseau") {
        const bool on = yes(v);
        return change(key == "ecritures" ? "\xC3\x89" "crire dans l'automate" : key == "demo" ? "Serveur de d\xC3\xA9monstration" : "Serveur accessible du r\xC3\xA9seau",
                      [&](hmi::Communication& x) {
                          if (key == "ecritures") x.writes = on;
                          else if (key == "demo") x.demoServer = on;
                          else x.demoAllInterfaces = on;
                      });
    }
    return fail("r\xC3\xA9glage inconnu : " + key);
}

bool HmiCommPane::addAddress(const std::string& rawVariable, const std::string& rawAddress, const std::string& rawType, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& c = doc_->project.comm;
    std::string variable = trimmed(rawVariable);
    if (!validPath(variable)) return fail("variable \xC2\xAB " + variable + " \xC2\xBB : un nom ou un chemin (Pression, Armoires[0].ana.PT1.mes)");
    // Un nom deja pris : Nouvelle_variable_2...
    const auto taken = [&](const std::string& n) {
        return std::any_of(c.addresses.begin(), c.addresses.end(), [&](const hmi::CommAddress& a) { return same(a.variable, n); });
    };
    if (taken(variable)) {
        if (rawVariable != "Nouvelle_variable") return fail(variable + " a d\xC3\xA9j\xC3\xA0 sa ligne");
        for (int k = 2; taken(variable); ++k) variable = "Nouvelle_variable_" + std::to_string(k);
    }
    const std::string type = upperOf(trimmed(rawType));
    if (!type.empty() && sim::typeFromName(type) == sim::Type::Unknown) return fail("type \xC2\xAB " + type + " \xC2\xBB inconnu (INT, REAL, BOOL...)");
    std::string address = trimmed(rawAddress);
    if (address.empty()) {
        // La prochaine adresse libre apres tout ce que le plan place.
        std::size_t end = 1000;
        const auto plan = currentPlan();
        for (const auto& p : plan.points())
            if (p.area == hmi::comm::Area::Holding) end = std::max(end, static_cast<std::size_t>(p.offset) + p.span());
        end = (end + 9) / 10 * 10;
        const auto t = type.empty() ? sim::Type::Int : sim::typeFromName(type);
        address = (t == sim::Type::Real ? "%MF" : (t == sim::Type::DInt || t == sim::Type::UDInt || t == sim::Type::DWord || t == sim::Type::Time) ? "%MD" : "%MW")
                  + std::to_string(end);
    }
    hmi::comm::Point probe;
    std::string reason;
    const auto t = type.empty() ? hmi::comm::typeOfAddress(address) : sim::typeFromName(type);
    if (t == sim::Type::Unknown || !hmi::comm::placeAddress(address, t, 16, probe, &reason))
        return fail("adresse \xC2\xAB " + address + " \xC2\xBB : " + (reason.empty() ? std::string("illisible") : reason));
    hmi::CommAddress a;
    a.variable = variable;
    a.address = probe.address;
    a.type = type;
    if (!change("Ajouter " + variable + " \xC3\xA0 la table des adresses", [&](hmi::Communication& x) { x.addresses.push_back(a); })) return false;
    selectAddress(variable);
    say(variable + " : " + a.address + " (" + probe.modbusText() + ")");
    return true;
}

bool HmiCommPane::removeAddress(const std::string& variable, std::string* why) {
    const auto& c = doc_->project.comm;
    if (std::none_of(c.addresses.begin(), c.addresses.end(), [&](const hmi::CommAddress& a) { return same(a.variable, variable); })) {
        const std::string m = "pas de ligne pour " + variable;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (!change("Retirer " + variable + " de la table des adresses",
                [&](hmi::Communication& x) { std::erase_if(x.addresses, [&](const hmi::CommAddress& a) { return same(a.variable, variable); }); }))
        return false;
    say(variable + " retir\xC3\xA9" "e de la table (Ctrl+Z la rend).");
    return true;
}

bool HmiCommPane::setAddressField(const std::string& variable, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& c = doc_->project.comm;
    const auto it = std::find_if(c.addresses.begin(), c.addresses.end(), [&](const hmi::CommAddress& a) { return same(a.variable, variable); });
    if (it == c.addresses.end()) return fail("pas de ligne pour " + variable);
    hmi::CommAddress next = *it;
    const std::string v = trimmed(raw);
    if (key == "variable") {
        if (!validPath(v)) return fail("variable \xC2\xAB " + v + " \xC2\xBB : un nom ou un chemin (Pression, Armoires[0].ana.PT1.mes)");
        if (!same(v, variable) && std::any_of(c.addresses.begin(), c.addresses.end(), [&](const hmi::CommAddress& a) { return same(a.variable, v); }))
            return fail(v + " a d\xC3\xA9j\xC3\xA0 sa ligne");
        next.variable = v;
    } else if (key == "adresse" || key == "type") {
        if (key == "adresse") next.address = v;
        else next.type = upperOf(v);
        if (!next.type.empty() && sim::typeFromName(next.type) == sim::Type::Unknown) return fail("type \xC2\xAB " + v + " \xC2\xBB inconnu (INT, REAL, BOOL...)");
        hmi::comm::Point probe;
        std::string reason;
        sim::Type t = next.type.empty() ? sim::Type::Unknown : sim::typeFromName(next.type);
        if (t == sim::Type::Unknown && hosts_.runtime && hosts_.runtime()) {
            sim::Value val;
            if (hosts_.runtime()->get(next.variable, val)) t = val.type();
        }
        if (t == sim::Type::Unknown) t = hmi::comm::typeOfAddress(next.address);
        if (t == sim::Type::Unknown || !hmi::comm::placeAddress(next.address, t, 16, probe, &reason))
            return fail("adresse \xC2\xAB " + next.address + " \xC2\xBB : " + (reason.empty() ? std::string("illisible") : reason));
        if (key == "adresse") next.address = probe.address;
    } else if (key == "lecture_seule") {
        next.readOnly = yes(v);
    } else if (key == "description") {
        next.description = v;
    } else {
        return fail("champ inconnu : " + key);
    }
    if (!change(variable + " : " + key, [&](hmi::Communication& x) {
            for (auto& a : x.addresses)
                if (same(a.variable, variable)) a = next;
        }))
        return false;
    if (key == "variable") selectAddress(next.variable);
    return true;
}

std::size_t HmiCommPane::propose() {
    const auto plan = currentPlan();
    sim::Runtime* rt = hosts_.runtime ? hosts_.runtime() : nullptr;
    if (!rt) {
        say("Proposer : le simulateur du programme n'est pas pr\xC3\xAAt (il dit le type de chaque variable).", true);
        return 0;
    }
    std::size_t words = 1000, bits = 100;
    for (const auto& p : plan.points()) {
        if (p.area == hmi::comm::Area::Holding) words = std::max(words, static_cast<std::size_t>(p.offset) + p.span());
        if (p.area == hmi::comm::Area::Coils) bits = std::max(bits, static_cast<std::size_t>(p.offset) + p.span());
    }
    words = (words + 9) / 10 * 10;
    bits = (bits + 9) / 10 * 10;
    std::vector<hmi::CommAddress> rows;
    for (const auto& path : hmi::comm::projectPlcPaths(doc_->project)) {
        if (plan.resolve(path) || plan.whyNot(path).empty()) continue;
        sim::Value v;
        if (!rt->get(path, v) || v.type() == sim::Type::Unknown) continue;
        hmi::CommAddress a;
        a.variable = path;
        a.type = std::string(sim::toString(v.type()));
        switch (v.type()) {
            case sim::Type::Bool: a.address = "%M" + std::to_string(bits++); break;
            case sim::Type::Real:
                words += words % 2;
                a.address = "%MF" + std::to_string(words);
                words += 2;
                break;
            case sim::Type::DInt: case sim::Type::UDInt: case sim::Type::DWord: case sim::Type::Time:
                words += words % 2;
                a.address = "%MD" + std::to_string(words);
                words += 2;
                break;
            case sim::Type::String:
                a.address = "%MW" + std::to_string(words);
                words += 8;
                a.type = "STRING[16]";
                break;
            default: a.address = "%MW" + std::to_string(words++); break;
        }
        a.description = "Propos\xC3\xA9" "e : le programme la recopie ici";
        rows.push_back(std::move(a));
    }
    if (rows.empty()) {
        say("Rien \xC3\xA0 proposer : chaque variable de l'automate que l'IHM utilise a d\xC3\xA9j\xC3\xA0 sa place.");
        return 0;
    }
    const std::size_t n = rows.size();
    if (!change("Proposer " + std::to_string(n) + " adresse(s)", [&](hmi::Communication& x) {
            for (auto& a : rows) x.addresses.push_back(a);
        }))
        return 0;
    selectAddress(rows.front().variable);
    say(std::to_string(n) + " variable(s) plac\xC3\xA9" "e(s) \xC3\xA0 des adresses libres : le programme doit les y recopier (Exporter le plan pour l'automaticien).");
    return n;
}

bool HmiCommPane::test() {
    reportLines_.clear();
    reportTones_.clear();
    const auto lines = CommHost::test(doc_->project.comm, hosts_.plc ? hosts_.plc() : nullptr, hosts_.runtime ? hosts_.runtime() : nullptr);
    bool ok = false;
    for (const auto& l : lines) {
        reportLines_.push_back(l.text);
        reportTones_.push_back(l.tone);
        ok = ok || (l.tone == 1 && l.text.rfind("Connexion", 0) == 0);
    }
    std::vector<std::vector<std::string>> rows;
    for (const auto& l : reportLines_) rows.push_back({l});
    const auto tones = reportTones_;
    reportModel_ = std::make_shared<Rows>(std::vector<std::string>{"Compte rendu de l'essai"}, std::move(rows), [tones](ui::RowIndex r, std::size_t) {
        ui::CellStyle st;
        if (r >= tones.size()) return st;
        st.fgTone = toneOf(tones[r]);
        if (tones[r] == 1) st.icon = ui::Icon::Ok;
        if (tones[r] == 3) st.icon = ui::Icon::Error;
        if (tones[r] == 2) st.icon = ui::Icon::Warning;
        return st;
    });
    report_->setModel(reportModel_);
    tabs_->setCurrentIndex(TTest);
    tabs_->setTabBadge(TTest, ok ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec", ok ? ui::Tone::Ok : ui::Tone::Error);
    const auto& c = doc_->project.comm;
    say(ok ? "Essai r\xC3\xA9ussi : l'automate " + c.host + ":" + std::to_string(c.port) + " r\xC3\xA9pond."
           : "Essai en \xC3\xA9" "chec : " + (reportLines_.size() > 1 ? reportLines_[1] : std::string("voir le compte rendu")),
        !ok);
    return ok;
}

bool HmiCommPane::exportPlan(std::string* where) {
    const auto& p = doc_->project;
    const auto& c = p.comm;
    const auto plan = currentPlan();
    hmi::ExportTable table;
    table.title = "Plan d'adressage Modbus";
    table.subtitle = p.config.name + " - " + hmi::wallStamp().substr(0, 19);
    table.headers = {"Variable", "Adresse", "Type", "Place Modbus", "Acc\xC3\xA8s", "Origine", "Description"};
    for (const auto& pt : plan.points())
        table.rows.push_back({pt.name, pt.address, pt.typeName, pt.modbusText(), pt.writable ? "lecture, \xC3\xA9" "criture" : "lecture seule", pt.origin,
                              pt.description});
    for (const auto& [name, why] : plan.refused())
        if (why.rfind("non localis", 0) != 0) table.rows.push_back({name, "\xE2\x80\x94", "\xE2\x80\x94", "refus\xC3\xA9" "e : " + why, "\xE2\x80\x94", "\xE2\x80\x94", ""});
    table.notes = {"Liaison : " + modeLabel(c) + " - " + c.host + ":" + std::to_string(c.port) + ", esclave " + std::to_string(c.unit),
                   "Ordre des mots (32 bits) : " + orderLabel(c) + " ; %MW i = registre i (fonction 3), %M i = bit i (fonction 1)",
                   "Les variables propos\xC3\xA9" "es (Description) : le programme doit les recopier \xC3\xA0 leur adresse."};
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName("plan_modbus_" + hmi::wallStamp().substr(0, 10), hmi::ExportFormat::Excel);
    rq.format = "Excel";
    rq.source = "communication";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, hmi::ExportFormat::Excel));
    rq.origin = "Configuration > Communication";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Plan d'adressage \xC3\xA9" "crit : " + path : "Plan impossible \xC3\xA0 \xC3\xA9" "crire" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiCommPane::rebuildPlcProperties(std::vector<PG::Category>& cats) {
    const auto& c = doc_->project.comm;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setSetting(key, std::string(v));
        };
    };
    PG::Category link;
    link.name = "Automate du projet";
    link.properties.push_back(prop("Mode", modeLabel(c), PG::ValueType::Enum, commit("mode"),
                                   "Simulateur : l'IHM lit l'automate simul\xC3\xA9 de l'application. Modbus TCP : un automate r\xC3\xA9" "el (M340, M580...) "
                                   "\xC3\xA0 cette adresse ; les variables s'y trouvent par leur adresse (le plan d'adressage).",
                                   {"Simulateur", "Modbus TCP"}));
    link.properties.push_back(prop("Adresse IP", c.host, PG::ValueType::Text, commit("hote"), "L'adresse de l'automate : 192.168.1.10 ; ou un nom."));
    link.properties.push_back(prop("Port", std::to_string(c.port), PG::ValueType::Integer, commit("port"), "502 : le port Modbus TCP."));
    link.properties.push_back(prop("Esclave (Unit Id)", std::to_string(c.unit), PG::ValueType::Integer, commit("esclave"),
                                   "255 : l'UC d'un M340 ou d'un M580 ; derri\xC3\xA8re une passerelle, le num\xC3\xA9ro de l'esclave."));
    link.properties.push_back(prop("D\xC3\xA9lai de r\xC3\xA9ponse (ms)", std::to_string(c.timeoutMs), PG::ValueType::Integer, commit("delai"),
                                   "Au-del\xC3\xA0, la requ\xC3\xAAte est perdue ; deux de suite : la liaison est coup\xC3\xA9" "e, puis refaite."));
    link.properties.push_back(prop("P\xC3\xA9riode de scrutation (ms)", std::to_string(c.periodMs), PG::ValueType::Integer, commit("periode"),
                                   "Tout ce que montrent les vues ouvertes est relu \xC3\xA0 chaque p\xC3\xA9riode ; une \xC3\xA9" "criture part tout de suite."));
    link.properties.push_back(prop("Nouvel essai (s)", std::to_string(c.retryS), PG::ValueType::Integer, commit("reessai"),
                                   "Automate injoignable : une nouvelle connexion toutes les N secondes."));
    link.properties.push_back(prop("Mauvaise apr\xC3\xA8s (s)", std::to_string(c.badAfterS), PG::ValueType::Integer, commit("mauvaise"),
                                   "Liaison perdue : les valeurs restent \xC2\xAB anciennes \xC2\xBB N secondes, puis deviennent \xC2\xAB mauvaises \xC2\xBB."));
    link.properties.push_back(prop("\xC3\x89" "crire dans l'automate", c.writes ? "TRUE" : "FALSE", PG::ValueType::Boolean, commit("ecritures"),
                                   "D\xC3\xA9" "coch\xC3\xA9 : lecture seule - les commandes de l'IHM ne changent rien dans l'automate (une mise en service prudente)."));
    cats.push_back(std::move(link));
    PG::Category rq;
    rq.name = "Requ\xC3\xAAtes";
    rq.properties.push_back(prop("Ordre des mots (32 bits)", orderLabel(c), PG::ValueType::Enum, commit("ordre"),
                                 "Un DINT, un REAL occupent deux mots : Schneider met le poids faible d'abord (%MD10 = %MW10 faible, %MW11 fort).",
                                 {"poids faible d'abord (Schneider)", "poids fort d'abord"}));
    rq.properties.push_back(prop("Mots par requ\xC3\xAAte", std::to_string(c.maxWords), PG::ValueType::Integer, commit("mots"), "125 au plus (la norme)."));
    rq.properties.push_back(prop("Bits par requ\xC3\xAAte", std::to_string(c.maxBits), PG::ValueType::Integer, commit("bits"), "2000 au plus."));
    rq.properties.push_back(prop("Regrouper \xC3\xA0 moins de (mots)", std::to_string(c.gap), PG::ValueType::Integer, commit("ecart"),
                                 "Deux variables s\xC3\xA9par\xC3\xA9" "es de moins de N mots : lues d'une seule requ\xC3\xAAte (le trou avec)."));
    cats.push_back(std::move(rq));
    PG::Category demo;
    demo.name = "Serveur de d\xC3\xA9monstration";
    demo.properties.push_back(prop("Exposer le simulateur", c.demoServer ? "TRUE" : "FALSE", PG::ValueType::Boolean, commit("demo"),
                                   "Le simulateur r\xC3\xA9pond en Modbus TCP, au m\xC3\xAAme plan d'adressage : l'IHM (Modbus TCP vers 127.0.0.1) "
                                   "ou une supervision s'y connectent comme \xC3\xA0 un vrai automate."));
    demo.properties.push_back(prop("Port du serveur", std::to_string(c.demoPort), PG::ValueType::Integer, commit("demo_port"),
                                   "5020 par d\xC3\xA9" "faut (502 demande des droits d'administrateur sous Linux)."));
    demo.properties.push_back(prop("Accessible du r\xC3\xA9seau", c.demoAllInterfaces ? "TRUE" : "FALSE", PG::ValueType::Boolean, commit("demo_reseau"),
                                   "D\xC3\xA9" "coch\xC3\xA9 : ce poste seulement (127.0.0.1). Coch\xC3\xA9 : d'autres postes aussi (toutes les cartes r\xC3\xA9seau)."));
    shownDemoState_ = demoState();
    demo.properties.push_back(prop("\xC3\x89tat du serveur", shownDemoState_, PG::ValueType::ReadOnly));
    cats.push_back(std::move(demo));

    const std::string variable = selectedAddress();
    const auto it = std::find_if(c.addresses.begin(), c.addresses.end(), [&](const hmi::CommAddress& a) { return same(a.variable, variable); });
    if (it != c.addresses.end()) {
        const std::string key = it->variable;
        const auto field = [this, key](const char* f) {
            return [this, key, f](std::string_view v) {
                message_.clear();
                return setAddressField(key, f, std::string(v));
            };
        };
        PG::Category row;
        row.name = "Variable choisie (table des adresses)";
        row.properties.push_back(prop("Variable", it->variable, PG::ValueType::Text, field("variable"),
                                      "Un nom ou un chemin tel que l'\xC3\xA9" "crivent les vues : Pression, Armoires[0].ana.PT1.mes."));
        row.properties.push_back(prop("Adresse", it->address, PG::ValueType::Text, field("adresse"),
                                      "%MW100 (INT), %MD20 (DINT), %MF20 (REAL), %M5 (BOOL), %MW10.3 (le bit 3 du mot 10), %IW4, %I7 (lecture seule)."));
        std::vector<std::string> types(std::begin(kTypes), std::end(kTypes));
        row.properties.push_back(prop("Type", it->type, PG::ValueType::Enum, field("type"),
                                      "Vide : celui du programme (le simulateur le dit), sinon celui que dit l'adresse.", types));
        row.properties.push_back(prop("Lecture seule", it->readOnly ? "TRUE" : "FALSE", PG::ValueType::Boolean, field("lecture_seule"),
                                      "L'IHM la lit, ne l'\xC3\xA9" "crit jamais."));
        row.properties.push_back(prop("Description", it->description, PG::ValueType::Text, field("description")));
        const auto plan = currentPlan();
        if (const auto p = plan.resolve(it->variable)) {
            row.properties.push_back(prop("Place Modbus", p->modbusText(), PG::ValueType::ReadOnly));
        } else {
            row.properties.push_back(prop("Refus\xC3\xA9" "e", plan.whyNot(it->variable), PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(row));
    }
}

std::string HmiCommPane::demoState() const {
    const auto& c = doc_->project.comm;
    auto* host = hosts_.comm ? hosts_.comm() : nullptr;
    if (host && !host->demoError().empty()) return "impossible : " + host->demoError();
    if (host && host->running())
        return "en marche sur " + std::string(c.demoAllInterfaces ? "toutes les cartes" : "127.0.0.1") + ":" + std::to_string(host->demoPort())
               + (host->demoMute() ? " (coup\xC3\xA9)" : "");
    return "arr\xC3\xAAt\xC3\xA9";
}

} // namespace app
