// =============================================================================
//  app/hmi/HmiExprPage.cpp - 1.11 (chantier T3, D5) : voir HmiExprPage.hpp
// =============================================================================
#include "HmiExprPage.hpp"

namespace app::exprpage {

namespace {

constexpr std::string_view kTry    = "essayer:";
constexpr std::string_view kTryOn  = "essayer@";
constexpr std::string_view kInsert = "inserer:";
constexpr std::string_view kTopic  = "expr-";
// Tranche 25 (avec REP) : une phrase, un exemple que le banc calcule (V[1].Pos vaut 85 : VRAI).
constexpr std::string_view kMarkersLine =
    "Un rep\xC3\xA8re $\xE2\x80\xA6$ marque ce qui varie quand on duplique (Dupliquer\xE2\x80\xA6, Ctrl+D) ; il ne change pas le "
    "calcul : =$V[1].Pos$ > 10 se calcule comme =V[1].Pos > 10.";

ui::HelpBlock block(ui::HelpBlockKind kind, std::string text) {
    ui::HelpBlock b;
    b.kind = kind;
    b.text = std::move(text);
    return b;
}

bool startsWith(std::string_view s, std::string_view p) noexcept { return s.substr(0, p.size()) == p; }

} // namespace

std::string tryTarget(std::string_view source, std::string_view caseKey) {
    if (caseKey.empty()) return std::string(kTry) + std::string(source);
    return std::string(kTryOn) + std::string(caseKey) + ":" + std::string(source);
}

std::optional<Action> parseTarget(std::string_view t) {
    Action a;
    if (startsWith(t, kTry)) {
        a.kind = Action::Kind::Try;
        a.text = std::string(t.substr(kTry.size()));
        return a;
    }
    if (startsWith(t, kTryOn)) {
        const auto rest  = t.substr(kTryOn.size());
        const auto colon = rest.find(':');          // une cle n'a pas de ':' ; la source, si
        if (colon == std::string_view::npos || colon == 0) return std::nullopt;
        a.kind    = Action::Kind::Try;
        a.typeKey = std::string(rest.substr(0, colon));
        a.text    = std::string(rest.substr(colon + 1));
        return a;
    }
    if (startsWith(t, kInsert)) {
        a.kind = Action::Kind::Insert;
        a.text = std::string(t.substr(kInsert.size()));
        return a;
    }
    if (startsWith(t, kTopic) && hmi::exprguide::find(t) != nullptr) {
        a.kind    = Action::Kind::Open;
        a.typeKey = std::string(hmi::exprguide::find(t)->key);
        return a;
    }
    return std::nullopt;
}

std::string openPageTarget(std::string_view typeKey) { return "expr:" + std::string(typeKey); }

ui::HelpArticle article(const hmi::exprguide::TypeEntry& t, ArticleOptions options) {
    namespace eg = hmi::exprguide;
    const bool field = options.trialField;      // tranche 11 : faux dans le centre de T2 (pas de champ d'essai)
    ui::HelpArticle a;
    a.blocks.push_back(block(ui::HelpBlockKind::Title, std::string(t.title)));
    a.blocks.push_back(block(ui::HelpBlockKind::Subtitle, "Expressions \xC2\xB7 type attendu : " + std::string(t.wanted)));
    a.blocks.push_back(block(ui::HelpBlockKind::Lead, std::string(t.summary)));
    if (!field) {
        auto open = block(ui::HelpBlockKind::Links, {});
        open.links.push_back({std::string(kOpenPageLabel), openPageTarget(t.key),
                              "La page des expressions s\xE2\x80\x99ouvre sur " + std::string(t.title)
                                  + " : son champ \xC2\xAB Essaie ici \xC2\xBB \xC3\xA9value en direct, avec le vrai moteur."});
        a.blocks.push_back(std::move(open));
    }

    a.blocks.push_back(block(ui::HelpBlockKind::Heading, "Syntaxe"));
    a.blocks.push_back(block(ui::HelpBlockKind::Code, std::string(t.syntax)));
    // 1.11 (chantier T3, tranche 25, avec REP : decisions 60 et 72 a 76) : la ligne des reperes. Les $ d'un repere sont
    // transparents pour le calcul (hmi::markers) ; ce que fait la case « Suivre V[0] » de Dupliquer est dans l'aide de
    // Dupliquer (T2), pas ici. Ce commit n'entre qu'avec REP : sans lui, l'exemple serait une erreur de syntaxe.
    a.blocks.push_back(block(ui::HelpBlockKind::Paragraph, std::string(kMarkersLine)));

    if (!t.operators.empty()) {
        a.blocks.push_back(block(ui::HelpBlockKind::Heading, "Op\xC3\xA9rateurs"));
        auto ops = block(ui::HelpBlockKind::Links, {});
        ops.label = field ? "Un clic l\xE2\x80\x99" "ajoute au champ d\xE2\x80\x99" "essai"
                          : "Un clic l\xE2\x80\x99" "essaie dans la page des expressions";
        for (const auto& o : t.operators)
            ops.links.push_back({std::string(o.chip), std::string(kInsert) + std::string(o.chip), std::string(o.text)});
        a.blocks.push_back(std::move(ops));
    }

    if (!t.functions.empty()) {
        a.blocks.push_back(block(ui::HelpBlockKind::Heading, "Fonctions utiles"));
        std::string table = "Fonction\tRend\tCe qu\xE2\x80\x99" "elle fait";
        for (const auto& f : t.functions)
            table += "\n" + std::string(f.call) + "\t" + std::string(f.returns) + "\t" + std::string(f.does);
        a.blocks.push_back(block(ui::HelpBlockKind::Table, std::move(table)));
    }

    if (!t.examples.empty()) {
        a.blocks.push_back(block(ui::HelpBlockKind::Heading, "Exemples"));
        for (const auto& x : t.examples) {
            a.blocks.push_back(block(ui::HelpBlockKind::Code, std::string(x.source)));
            auto go = block(ui::HelpBlockKind::Links, {});
            go.label = std::string(x.caption);
            go.links.push_back({"Essayer \xE2\x80\xBA", tryTarget(x.source),
                                field ? "Le mettre dans le champ d\xE2\x80\x99" "essai"
                                      : "La page des expressions s\xE2\x80\x99ouvre, l\xE2\x80\x99" "exemple dans son champ \xC2\xAB Essaie ici \xC2\xBB"});
            a.blocks.push_back(std::move(go));
        }
    }

    if (!t.mistakes.empty()) {
        a.blocks.push_back(block(ui::HelpBlockKind::Heading, "Erreurs courantes"));
        for (const auto& m : t.mistakes) {
            auto c = block(ui::HelpBlockKind::Callout,
                           "\xE2\x9C\x95 " + std::string(m.wrong) + "\n\xE2\x9C\x93 " + std::string(m.right) + "\n" + std::string(m.reason));
            c.severity = m.trap ? 1 : 2;
            c.label = m.trap ? "Pi\xC3\xA8ge : se lit, mais ne fait pas ce qu\xE2\x80\x99" "on croit" : "Erreur courante";
            a.blocks.push_back(std::move(c));
            auto go = block(ui::HelpBlockKind::Links, {});
            go.links.push_back({"Essayer le faux", tryTarget(m.wrong, m.caseKey),
                                field ? "Le champ d\xE2\x80\x99" "essai souligne l\xE2\x80\x99" "erreur et dit pourquoi"
                                      : "La page des expressions s\xE2\x80\x99ouvre : son champ souligne l\xE2\x80\x99" "erreur et dit pourquoi"});
            go.links.push_back({"Essayer le juste", tryTarget(m.right, m.caseKey), {}});
            a.blocks.push_back(std::move(go));
        }
    }

    auto others = block(ui::HelpBlockKind::Links, {});
    others.label = "Les autres types";
    for (const auto& o : eg::all())
        if (o.key != t.key) others.links.push_back({std::string(o.title), eg::topicKey(o), std::string(o.summary)});
    a.blocks.push_back(std::move(others));
    return a;
}

std::vector<ListRow> typeList() {
    std::vector<ListRow> rows;
    for (const auto& t : hmi::exprguide::all())
        rows.push_back({std::string(t.key), std::string(t.glyph), std::string(t.title), std::string(t.summary)});
    return rows;
}

} // namespace app::exprpage
