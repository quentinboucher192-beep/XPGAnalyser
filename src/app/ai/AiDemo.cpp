// =============================================================================
//  app/ai/AiDemo.cpp - voir AiDemo.hpp
// =============================================================================
#include "AiDemo.hpp"

#include "../../hmi/HmiEdit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app::ai {

namespace {

// Les mots accentues qui reviennent, ecrits une fois.
const char* const kE = "\xC3\xA9";      // e accent aigu
const char* const kOpen = "\xC2\xAB ";  // guillemet ouvrant et son espace
const char* const kClose = " \xC2\xBB";
const char* const kRed = "#E5534B";     // le rouge du logiciel (colorFault, colorAlarm)

bool has(const std::string& folded, std::string_view word) { return folded.find(word) != std::string::npos; }

std::string num(double d) { return hmi::formatNumber(d); }

bool isButtonKind(hmi::Kind k) {
    switch (k) {
        case hmi::Kind::Button: case hmi::Kind::PushButton: case hmi::Kind::IlluminatedButton: case hmi::Kind::Switch:
        case hmi::Kind::SystemButton: case hmi::Kind::ExportButton: case hmi::Kind::LogoutButton: case hmi::Kind::LoginMenuButton:
            return true;
        default:
            return false;
    }
}

const hmi::Object* objectById(const hmi::View& v, hmi::Id id) {
    for (const auto& o : v.objects)
        if (o.id == id) return &o;
    return nullptr;
}

// Les objets du haut (pas les enfants d'un groupe) choisis, dans l'ordre de la vue.
std::vector<const hmi::Object*> selected(const DemoContext& c) {
    std::vector<const hmi::Object*> out;
    if (!c.view) return out;
    for (const auto id : c.selection)
        if (const auto* o = objectById(*c.view, id)) out.push_back(o);
    return out;
}

Operation create(hmi::Kind k, std::string name, double x, double y, double w, double h,
                 std::vector<std::pair<std::string, std::string>> more = {}) {
    Operation op;
    op.action = Operation::Action::Create;
    op.kind = std::string(hmi::kindKey(k));
    op.name = std::move(name);
    op.props = {{"x", num(x)}, {"y", num(y)}, {"w", num(w)}, {"h", num(h)}};
    for (auto& p : more) op.props.push_back(std::move(p));
    return op;
}

Proposal baseProposal(const DemoContext& c, std::string_view request, std::string summary) {
    Proposal p;
    p.id = newId("op");
    p.view = c.view ? c.view->name : std::string{};
    p.viewId = c.view ? c.view->id : hmi::kNoId;
    p.fingerprint = c.view ? fingerprint(*c.view) : std::string{};
    p.request = std::string(request);
    p.summary = std::move(summary);
    return p;
}

// Le bas des objets deja la (0 : vue vide) - la station se pose dessous s'il y a la place.
double contentBottom(const hmi::View& v) {
    double b = 0;
    for (const auto& o : v.objects)
        if (o.parent == hmi::kNoId) b = std::max(b, hmi::edit::rotatedBounds(o).bottom());
    return b;
}
double contentRight(const hmi::View& v) {
    double r = 0;
    for (const auto& o : v.objects)
        if (o.parent == hmi::kNoId) r = std::max(r, hmi::edit::rotatedBounds(o).right());
    return r;
}

std::string viewName(const DemoContext& c) { return c.view ? std::string(kOpen) + c.view->name + kClose : std::string("la vue"); }

DemoReply noView() {
    DemoReply r;
    r.text = std::string("Ouvre d'abord une vue dans l'") + kE + "diteur (double-clic sur une vue de l'arbre du projet) : "
             "je propose mes modifications sur la vue de l'onglet ouvert.";
    return r;
}

// ---- les demandes reconnues --------------------------------------------------------
DemoReply station(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    const hmi::View& v = *c.view;
    const double W = std::max(900, v.width), margin = 40, colW = (W - 2 * margin) / 3;
    double y0 = 24;
    const double used = contentBottom(v);
    if (used > 0 && used + 30 + 690 <= v.height) y0 = used + 30;
    Proposal p = baseProposal(c, request, "Synoptique de station de pompage");
    auto& ops = p.ops;
    ops.push_back(create(hmi::Kind::Text, "Titre_Station", margin, y0, 520, 40,
                         {{"text", "Station de pompage"}, {"fontSize", "26"}, {"align", "gauche"}}));
    const char* levels[3] = {"65", "40", "80"};
    int pumps = 0, buttons = 0;
    for (int col = 0; col < 3; ++col) {
        const double cx = margin + col * colW;
        const std::string n = std::to_string(col + 1);
        ops.push_back(create(hmi::Kind::Tank, "Cuve_" + n, cx + 40, y0 + 70, 150, 200,
                             {{"label", "Cuve " + n}, {"value", levels[col]}, {"max", "100"}, {"unit", "%"}}));
        ops.push_back(create(hmi::Kind::Bargraph, "Niveau_Cuve_" + n, cx + 214, y0 + 70, 56, 200,
                             {{"value", levels[col]}, {"unit", "%"}}));
        ops.push_back(create(hmi::Kind::Valve, "Vanne_" + n, cx + 75, y0 + 300, 80, 64, {{"label", "V" + n}}));
        ops.push_back(create(hmi::Kind::Pipe, "Collecteur_" + n, cx + 20, y0 + 392, colW - 80, 20, {}));
        const double half = (colW - 80) / 2;
        for (int k = 0; k < 2; ++k) {
            const std::string pn = n + std::to_string(k + 1);
            const double px = cx + 20 + k * half;
            ops.push_back(create(hmi::Kind::Pump, "Pompe_" + pn, px, y0 + 430, 90, 80, {{"label", "P" + pn}}));
            ops.push_back(create(hmi::Kind::PushButton, "Marche_P" + pn, px, y0 + 556, 96, 36,
                                 {{"text", "Marche"}, {"fill", "#2F7D4F"}, {"stroke", "#24603C"}, {"fontSize", "14"}}));
            ops.push_back(create(hmi::Kind::PushButton, "Arret_P" + pn, px, y0 + 600, 96, 36,
                                 {{"text", "Arr\xC3\xAAt"}, {"fill", "#7D3A36"}, {"stroke", "#5E2B28"}, {"fontSize", "14"}}));
            ++pumps;
            buttons += 2;
        }
    }
    DemoReply r;
    const int total = static_cast<int>(ops.size());
    r.text = std::string("## Synoptique de station de pompage\n") + "Je propose **" + std::to_string(total) + " objets** dans "
             + viewName(c) + ", en trois colonnes, une par cuve :\n"
             "- **3 cuves** (genre Cuve) avec leur niveau en %, et **3 bargraphes** de niveau \xC3\xA0 c\xC3\xB4t\xC3\xA9 ;\n"
             "- **3 vannes** de sortie et **3 tuyauteries** vers les pompes ;\n"
             "- **" + std::to_string(pumps) + " pompes**, deux par cuve (P11, P12, P21...) ;\n"
             "- **" + std::to_string(buttons) + " boutons \xC3\xA0 impulsion** : Marche et Arr\xC3\xAAt sous chaque pompe ;\n"
             "- un titre.\n"
             "Je n'ai associ\xC3\xA9 **aucune variable** : je n'invente pas de nom. Ensuite, s\xC3\xA9lectionne une pompe et demande "
             "par exemple `Associe cette pompe \xC3\xA0 la variable ...`.";
    r.proposal = std::move(p);
    return r;
}

DemoReply tankWithLevel(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    const hmi::View& v = *c.view;
    double x = contentRight(v) + 40, y = 80;
    if (x + 280 > v.width) {
        x = 60;
        y = std::min(contentBottom(v) + 30, std::max(20.0, v.height - 260.0));
    }
    int n = 1;
    while (true) {
        bool taken = false;
        for (const auto& o : v.objects)
            if (o.name == "Cuve_" + std::to_string(n)) taken = true;
        if (!taken) break;
        ++n;
    }
    const std::string id = std::to_string(n);
    Proposal p = baseProposal(c, request, "Cuve et indicateur de niveau");
    p.ops.push_back(create(hmi::Kind::Tank, "Cuve_" + id, x, y, 150, 200, {{"label", "Cuve " + id}, {"unit", "%"}, {"max", "100"}}));
    p.ops.push_back(create(hmi::Kind::Bargraph, "Niveau_Cuve_" + id, x + 174, y, 56, 200, {{"unit", "%"}}));
    DemoReply r;
    r.text = "Je propose **une cuve** (Cuve_" + id + ") et **son indicateur de niveau** (un bargraphe gradu\xC3\xA9 de 0 \xC3\xA0 100 %) "
             "\xC3\xA0 c\xC3\xB4t\xC3\xA9, dans " + viewName(c) + ", l\xC3\xA0 o\xC3\xB9 la vue est libre (x = " + num(x) + ", y = " + num(y) + ").\n"
             "Aucune variable n'est associ\xC3\xA9" "e : dites-moi laquelle relier au niveau.";
    r.proposal = std::move(p);
    return r;
}

DemoReply rightmostRed(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    const auto sel = selected(c);
    DemoReply r;
    if (sel.empty()) {
        r.text = "Je n'ai pas de s\xC3\xA9lection : choisis les objets dans la vue (Ctrl+clic pour plusieurs), puis redemande. "
                 "Je prendrai **celui qui est le plus \xC3\xA0 droite**.";
        return r;
    }
    const hmi::Object* right = sel.front();
    for (const auto* o : sel)
        if (hmi::edit::rotatedBounds(*o).cx() > hmi::edit::rotatedBounds(*right).cx()) right = o;
    std::string key;
    if (hmi::kindHasLevel(right->kind) && right->find("fillColor")) key = "fillColor";
    else if (hmi::kindIsSynoptic(right->kind) && right->find("colorOn")) key = "colorOn";
    else if (right->find("fill")) key = "fill";
    else if (right->find("textColor")) key = "textColor";
    if (key.empty()) {
        r.text = right->name + " (" + std::string(hmi::kindLabel(right->kind)) + ") n'a pas de couleur que je sache changer.";
        return r;
    }
    Proposal p = baseProposal(c, request, right->name + " en rouge");
    Operation op;
    op.action = Operation::Action::Modify;
    op.name = right->name;
    op.props = {{key, kRed}};
    p.ops.push_back(std::move(op));
    const char* what = key == "fillColor" ? "la couleur de son contenu" : key == "colorOn" ? "sa couleur en marche"
                     : key == "fill" ? "son remplissage" : "la couleur de son texte";
    r.text = "Sur les " + std::to_string(sel.size()) + " objets choisis, celui de droite est **" + right->name + "** ("
             + std::string(hmi::kindLabel(right->kind)) + "). Je propose de passer " + what + " (`" + key + "`) en rouge, `" + kRed
             + "` : le rouge des d\xC3\xA9" "fauts du logiciel.";
    r.proposal = std::move(p);
    return r;
}

DemoReply alignButtons(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    std::vector<const hmi::Object*> buttons;
    for (const auto* o : selected(c))
        if (isButtonKind(o->kind)) buttons.push_back(o);
    const bool fromSelection = !buttons.empty();
    if (!fromSelection)
        for (const auto& o : c.view->objects)
            if (o.parent == hmi::kNoId && isButtonKind(o.kind)) buttons.push_back(&o);
    DemoReply r;
    if (buttons.size() < 2) {
        r.text = "Je ne trouve pas de boutons \xC3\xA0 aligner dans " + viewName(c) + ".";
        return r;
    }
    if (buttons.size() != 4) {
        r.text = "Je vois **" + std::to_string(buttons.size()) + " boutons** " + (fromSelection ? "dans la s\xC3\xA9lection" : "dans la vue")
                 + " : lesquels sont les quatre ? S\xC3\xA9lectionne-les (Ctrl+clic), puis redemande.";
        return r;
    }
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto* b : buttons) {
        const auto box = b->box();
        minX = std::min(minX, box.x);
        maxX = std::max(maxX, box.x);
        minY = std::min(minY, box.y);
        maxY = std::max(maxY, box.y);
    }
    const bool row = (maxX - minX) >= (maxY - minY);
    Proposal p = baseProposal(c, request, "Aligner les quatre boutons");
    Operation align;
    align.action = Operation::Action::Align;
    align.edge = row ? "haut" : "gauche";
    for (const auto* b : buttons) align.objects.push_back(b->name);
    Operation spread = align;
    spread.action = Operation::Action::Distribute;
    spread.edge = row ? "horizontal" : "vertical";
    p.ops.push_back(std::move(align));
    p.ops.push_back(std::move(spread));
    std::string names;
    for (const auto* b : buttons) names += (names.empty() ? "" : ", ") + std::string("`") + b->name + "`";
    r.text = "Je propose d'aligner " + names + (row ? " sur leur **bord haut**" : " sur leur **bord gauche**")
             + ", puis de les r\xC3\xA9partir \xC3\xA0 **\xC3\xA9" "carts \xC3\xA9gaux** (" + (row ? "horizontalement" : "verticalement")
             + ") : les gestes Aligner et Distribuer de la barre d'outils.";
    r.proposal = std::move(p);
    return r;
}

std::string variableIn(std::string_view request) {
    // Le mot qui suit "variable", sinon le premier mot en majuscules avec un souligne.
    const std::string text(request);
    const std::string low = fold(request);
    auto token = [&](std::size_t from) {
        std::size_t i = from;
        while (i < text.size() && !(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
        std::size_t j = i;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_' || text[j] == '.')) ++j;
        while (j > i && text[j - 1] == '.') --j;
        return text.substr(i, j - i);
    };
    if (const auto at = low.find("variable"); at != std::string::npos) {
        // fold() ne change pas la longueur des mots sans accent : "variable" est a la meme place.
        const auto raw = text.find("variable");
        const auto t = token((raw != std::string::npos ? raw : at) + 8);
        if (!t.empty()) return t;
    }
    std::size_t i = 0;
    while (i < text.size()) {
        const auto t = token(i);
        if (t.empty()) break;
        if (t.find('_') != std::string::npos && std::any_of(t.begin(), t.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)); }))
            return t;
        i = text.find(t, i) + t.size();
    }
    return {};
}

DemoReply bindPump(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    DemoReply r;
    const std::string var = variableIn(request);
    if (var.empty()) {
        r.text = "Quelle variable ? \xC3\x89" "cris son nom exact, par exemple `Associe cette pompe \xC3\xA0 la variable Pompe_1_Marche`.";
        return r;
    }
    const hmi::Object* pump = nullptr;
    for (const auto* o : selected(c))
        if (o->kind == hmi::Kind::Pump) { pump = o; break; }
    if (!pump) {
        std::vector<const hmi::Object*> all;
        for (const auto& o : c.view->objects)
            if (o.kind == hmi::Kind::Pump) all.push_back(&o);
        if (all.size() == 1) pump = all.front();
        else {
            r.text = all.empty() ? "Il n'y a pas de pompe dans " + viewName(c) + "."
                                 : "Il y a " + std::to_string(all.size()) + " pompes dans " + viewName(c)
                                       + " : s\xC3\xA9lectionne celle \xC3\xA0 associer, puis redemande.";
            return r;
        }
    }
    Proposal p = baseProposal(c, request, pump->name + " \xE2\x86\x92 " + var);
    Operation op;
    op.action = Operation::Action::Bind;
    op.name = pump->name;
    op.variable = var;
    p.ops.push_back(std::move(op));
    if (c.project && variableExists(*c.project, var, c.env)) {
        r.text = "Je propose d'associer **" + pump->name + "** \xC3\xA0 la variable **" + var
                 + "** (sa propri\xC3\xA9t\xC3\xA9 `variable` : son \xC3\xA9tat de marche viendra de l\xC3\xA0).";
    } else {
        std::string near, others;
        std::size_t count = 0;
        if (c.project) {
            for (const auto& n : nearVariables(*c.project, var)) near += (near.empty() ? "" : ", ") + std::string("`") + n + "`";
            // Aucune ne ressemble : l'apercu propose quand meme les variables du projet - la reponse le dit.
            for (const auto& v : c.project->programs.variables) {
                if (++count <= 3) others += (others.empty() ? "" : ", ") + std::string("`") + v.name + "`";
            }
        }
        const std::string choose = !near.empty() ? " : les plus proches sont " + near
                                 : count == 0    ? std::string(" (le projet n'en a encore aucune)")
                                                 : " dans l'aper\xC3\xA7u (aucune ne ressemble \xC3\xA0 ce nom ; le projet en a " + std::to_string(count)
                                                       + " : " + others + (count > 3 ? ", \xE2\x80\xA6" : "") + ")";
        r.text = "**La variable " + var + " n'existe pas** dans ce projet, et je ne l'invente pas : une variable fausse ne "
                 "s'allumerait jamais en exploitation.\n"
                 "- choisis une variable existante" + choose + " ;\n"
                 "- ou demande **sa cr\xC3\xA9" "ation** dans les variables de l'IHM (BOOL).\n"
                 "L'aper\xC3\xA7u le propose ; rien ne s'applique tant que ce n'est pas d\xC3\xA9" "cid\xC3\xA9.";
    }
    r.proposal = std::move(p);
    return r;
}

DemoReply removeValves(std::string_view request, const DemoContext& c) {
    if (!c.view) return noView();
    DemoReply r;
    std::vector<std::string> names;
    for (const auto& o : c.view->objects)
        if (o.kind == hmi::Kind::Valve || o.kind == hmi::Kind::ThreeWayValve) names.push_back(o.name);
    if (names.empty()) {
        r.text = "Il n'y a pas de vanne dans " + viewName(c) + " : rien \xC3\xA0 supprimer.";
        return r;
    }
    Proposal p = baseProposal(c, request, "Supprimer " + std::to_string(names.size()) + (names.size() > 1 ? " vannes" : " vanne"));
    Operation op;
    op.action = Operation::Action::Remove;
    op.objects = names;
    p.ops.push_back(std::move(op));
    std::string list;
    for (const auto& n : names) list += (list.empty() ? "" : ", ") + std::string("`") + n + "`";
    r.text = "Je propose de supprimer **" + std::to_string(names.size()) + (names.size() > 1 ? " vannes**" : " vanne**") + " : " + list + ".\n"
             + (names.size() > 1 ? "C'est une suppression multiple : **une confirmation** te sera demand\xC3\xA9" "e avant d'appliquer. "
                                 : std::string{})
             + "Ctrl+Z la reprendra d'un coup.";
    r.proposal = std::move(p);
    return r;
}

DemoReply fallback(const DemoContext& c) {
    DemoReply r;
    r.text = "En d\xC3\xA9monstration, je reconnais seulement les demandes propos\xC3\xA9" "es ci-dessous. **Aucune IA n'est connect\xC3\xA9" "e** : "
             "une demande libre ne peut pas encore \xC3\xAAtre comprise. Choisis une suggestion, ou \xC3\xA9" "cris-la telle quelle.";
    if (!c.attachments.empty()) {
        std::string files;
        for (const auto& a : c.attachments) files += (files.empty() ? "" : ", ") + std::string("**") + a.name + "** (" + formatBytes(a.bytes) + ")";
        r.text = "J'ai bien re\xC3\xA7u " + files + ", copi\xC3\xA9" + (c.attachments.size() > 1 ? "s" : "")
                 + " dans le dossier de l'Assistant. En d\xC3\xA9monstration, leur contenu n'est pas lu.\n\n" + r.text;
    }
    r.suggestions = true;
    return r;
}

} // namespace

std::string fold(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            const auto d = static_cast<unsigned char>(text[i + 1]);
            ++i;
            if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            else if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F)) out += 'i';
            else if ((d >= 0xB2 && d <= 0xB6) || (d >= 0x92 && d <= 0x96)) out += 'o';
            else if ((d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C)) out += 'u';
            else out += ' ';
            continue;
        }
        if (c >= 0x80) {
            // Une autre lettre ou une apostrophe typographique : un blanc.
            if (c >= 0xC0) out += ' ';
            continue;
        }
        const char low = static_cast<char>(std::tolower(c));
        if (std::isspace(c) || low == '\'' || low == ',' || low == '.' || low == '!' || low == '?' || low == ';' || low == ':') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        out += low;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

const std::vector<std::string>& demoSuggestions() {
    static const std::vector<std::string> k = {
        "Cr\xC3\xA9" "e-moi un synoptique de station de pompage avec trois cuves, deux pompes par cuve, des vannes, des indicateurs de "
        "niveau et les boutons de commande associ\xC3\xA9s",
        "Ajoute une cuve avec un indicateur de niveau",
        "Mets celle de droite en rouge",
        "Aligne les quatre boutons",
        "Associe cette pompe \xC3\xA0 la variable PUMP_01_RUN",
        "Supprime les vannes",
    };
    return k;
}

DemoReply demoReply(std::string_view request, const DemoContext& c) {
    const std::string f = fold(request);
    if (has(f, "station") && has(f, "pompage")) return station(request, c);
    if (has(f, "cuve") && (has(f, "indicateur") || has(f, "niveau")) && (has(f, "ajout") || has(f, "cree") || has(f, "mets") || has(f, "pose")))
        return tankWithLevel(request, c);
    if (has(f, "rouge") && (has(f, "droite") || has(f, "selection") || has(f, "celle") || has(f, "celui")))
        return rightmostRed(request, c);
    if (has(f, "align") && has(f, "bouton")) return alignButtons(request, c);
    if ((has(f, "associ") || has(f, "relie") || has(f, "lie ")) && (has(f, "variable") || has(f, "pump") || has(f, "pompe")))
        return bindPump(request, c);
    if ((has(f, "supprim") || has(f, "efface") || has(f, "enleve") || has(f, "retire")) && has(f, "vanne")) return removeValves(request, c);
    return fallback(c);
}

std::string titleFor(std::string_view request) {
    const std::string f = fold(request);
    if (has(f, "station") && has(f, "pompage")) return "Station de pompage";
    if (has(f, "cuve")) return "Cuve et niveau";
    if (has(f, "rouge")) return "Couleur de la s\xC3\xA9lection";
    if (has(f, "align")) return "Aligner les boutons";
    if (has(f, "associ") || has(f, "variable")) return "Association de variable";
    if (has(f, "vanne")) return "Supprimer les vannes";
    std::string t(request.substr(0, std::min<std::size_t>(request.size(), 48)));
    // Pas de lettre coupee au milieu (UTF-8).
    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    if (!t.empty() && static_cast<unsigned char>(t.back()) >= 0xC0) t.pop_back();
    if (t.size() < request.size()) t += "\xE2\x80\xA6";
    return t.empty() ? std::string("Nouvelle conversation") : t;
}

std::vector<Conversation> demoConversations() {
    std::vector<Conversation> out;
    const std::time_t t = now();
    {
        Conversation c;
        c.id = newId("c");
        c.title = "D\xC3\xA9" "couvrir l'Assistant";
        c.demo = true;
        // 1.14.2 : la plus recente des deux - la premiere ouverture la montre (pas la conversation
        // perimee, et sa carte d'erreur, en premiere impression).
        c.created = isoDate(t - 86400 - 3600);
        c.modified = isoDate(t - 86400 - 3500);
        Message u;
        u.id = "m1";
        u.role = "utilisateur";
        u.date = c.created;
        u.text = "Que sais-tu faire ?";
        Message a;
        a.id = "m2";
        a.role = "assistant";
        a.date = isoDate(t - 86400 - 3596);
        a.state = "fini";
        a.text = "## Ce que montre cette d\xC3\xA9monstration\n"
                 "Je travaille sur **la vue ouverte** dans l'\xC3\xA9" "diteur, avec les vrais objets du logiciel (Cuve, Pompe, Vanne, "
                 "Bargraphe, Bouton \xC3\xA0 impulsion...). Je reconnais ces demandes :\n"
                 "- un **synoptique de station de pompage** complet ;\n"
                 "- **une cuve** avec son indicateur de niveau ;\n"
                 "- **celle de droite en rouge** (sur la s\xC3\xA9lection) ;\n"
                 "- **aligner les quatre boutons** ;\n"
                 "- **associer une pompe** \xC3\xA0 une variable - qui doit exister ;\n"
                 "- **supprimer les vannes** (avec confirmation).\n"
                 "Chaque proposition passe par l'**aper\xC3\xA7u** (mode Pr\xC3\xA9visualisation) ou s'applique tout de suite (mode Direct) ; "
                 "dans les deux cas, **Ctrl+Z** la reprend d'un coup. Aucune IA n'est connect\xC3\xA9" "e : rien ne quitte ce PC.";
        c.messages = {u, a};
        out.push_back(std::move(c));
    }
    {
        Conversation c;
        c.id = newId("c");
        c.title = "Station de pompage (ancienne vue)";
        c.demo = true;
        c.created = isoDate(t - 2 * 86400);
        c.modified = isoDate(t - 2 * 86400 + 100);
        c.project.view = "Vue_Station_Essai";
        Message u;
        u.id = "m1";
        u.role = "utilisateur";
        u.date = c.created;
        u.text = demoSuggestions().front();
        Message a;
        a.id = "m2";
        a.role = "assistant";
        a.date = isoDate(t - 2 * 86400 + 10);
        a.state = "fini";
        a.text = "## Synoptique de station de pompage\n"
                 "Je propose **31 objets** dans " + std::string(kOpen) + "Vue_Station_Essai" + kClose + " : 3 cuves et leurs bargraphes, "
                 "3 vannes, 3 tuyauteries, 6 pompes et 12 boutons \xC3\xA0 impulsion, et un titre.\n"
                 "*(Conversation de d\xC3\xA9monstration : cette vue n'existe pas dans ton projet - la proposition ne s'applique donc pas, "
                 "l'aper\xC3\xA7u le dit et propose de la redemander.)*";
        Proposal p;
        p.id = "op1";
        p.view = "Vue_Station_Essai";
        p.viewId = 0;
        p.fingerprint = "0000000000000000";
        p.request = u.text;
        p.summary = "Synoptique de station de pompage";
        p.create = 31;
        Operation op;
        op.action = Operation::Action::Create;
        op.kind = "Tank";
        op.name = "Cuve_1";
        op.props = {{"x", "80"}, {"y", "100"}};
        p.ops.push_back(op);
        a.proposal = std::move(p);
        c.messages = {u, a};
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace app::ai
