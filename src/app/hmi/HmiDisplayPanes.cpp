// Configuration > Langues, Configuration > Unites et formats (lot 13).
#include "HmiDisplayPanes.hpp"
#include "../ExportTarget.hpp"
#include "HmiPaneKit.hpp"

#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiLive.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../xls/Workbook.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <utility>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;

namespace {

enum : int { TAdd = 1, TRemove, TStart, TMissing, TExport, TImport, TTry };

std::string upperCode(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string percent(std::size_t n, std::size_t total) {
    if (total == 0) return "-";
    return std::to_string(n * 100 / total) + " %";
}

std::string fileSafe(std::string s) {
    for (auto& c : s)
        if (std::string_view("\\/:*?\"<>| ").find(c) != std::string_view::npos) c = '_';
    return s;
}

// Les trous d'un texte, tries (pour comparer une traduction a son origine).
std::vector<std::string> sortedHoles(std::string_view text) {
    auto h = hmi::templateHoles(text);
    std::sort(h.begin(), h.end());
    return h;
}

} // namespace

HmiLanguagesPane::HmiLanguagesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TAdd, HmiGlyph::Plus, "Ajouter une langue (English, Deutsch, Espa\xC3\xB1ol...)", "Ajouter une langue");
    tools->add(TRemove, HmiGlyph::Delete, "Retirer la langue choisie et ses traductions (Ctrl+Z les rend)", "Retirer");
    tools->add(TStart, HmiGlyph::Play, "La langue choisie au lancement de l'IHM (SYS.Language au d\xC3\xA9marrage)", "Langue de d\xC3\xA9marrage");
    tools->separator();
    tools->add(TMissing, HmiGlyph::Search, "Seulement les textes qu'il reste \xC3\xA0 traduire", "\xC3\x80 traduire");
    tools->separator();
    tools->add(TExport, HmiGlyph::Export, "Exporter les textes pour un traducteur (Excel, dossier exports/)", "Exporter (Excel)");
    tools->add(TImport, HmiGlyph::Import, "Importer les traductions d'un classeur Excel (le tableau export\xC3\xA9, rempli)", "Importer (Excel)");
    tools->separator();
    tools->add(TTry, HmiGlyph::View, "Essayer : la simulation s'ouvre dans la langue choisie", "Essayer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(TRemove, [this] {
        const auto code = selectedLanguage();
        return !code.empty() && !same(code, doc_->project.languages.source());
    });
    tools_->setEnabledWhen(TStart, [this] { return !selectedLanguage().empty(); });
    tools_->setEnabledWhen(TTry, [this] { return !selectedLanguage().empty(); });
    tools_->setCheckedWhen(TMissing, [this] { return onlyMissing_; });

    auto langs = std::make_unique<ui::TableView>(base + ".langs");
    langs->setColumns({{"Code", 62.f}, {"Langue", 98.f}, {"Traduits", 150.f, 60.f, true, true, true, ui::Align::End},
                       {"D\xC3\xA9marrage", 104.f, 60.f, true, false, true, ui::Align::Center}});
    langs->setSelectionMode(ui::SelectionMode::Single);
    langs_ = &static_cast<ui::TableView&>(addChild(std::move(langs)));
    auto texts = std::make_unique<ui::TableView>(base + ".texts");
    texts->setSelectionMode(ui::SelectionMode::Single);
    texts_ = &static_cast<ui::TableView&>(addChild(std::move(texts)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const std::string code = selectedLanguage();
        switch (a) {
            case TAdd:
                if (hosts_.askAdd) hosts_.askAdd();
                else say("Ajouter une langue : son code (en, de, es...) - ici, par un script ou les tests.", true);
                break;
            case TRemove: if (!code.empty()) (void)removeLanguage(code); break;
            case TStart: if (!code.empty()) (void)setStartLanguage(code); break;
            case TMissing: setOnlyMissing(!onlyMissing_); break;
            // Lot 7 : ou exporter (ExportTarget.hpp) - exports/ par defaut, le bouton ... ailleurs.
            case TExport: if (!askExportTarget("les textes \xC3\xA0 traduire (Excel)", "Classeurs Excel|*.xlsx", [this] { (void)exportTranslations(); })) (void)exportTranslations(); break;
            case TImport:
                if (hosts_.askImport) hosts_.askImport();
                else say("Importer : le chemin du classeur est demand\xC3\xA9 par l'application.", true);
                break;
            case TTry:
                if (!code.empty() && hosts_.tryIt) {
                    hosts_.tryIt(code);
                    say("Simulation en " + hmi::languageName(code) + " : les textes sans traduction restent dans la langue du projet.");
                }
                break;
            default: break;
        }
    });
    links_ += langs_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += texts_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    // Lot 20 : coller des traductions depuis Excel. Le texte d'origine dit
    // quelle ligne (il vient des vues : il ne se cree pas ici) ; chaque langue
    // se reconnait a son titre, a son nom ou a son code (en, de...).
    texts_->setSelectionMode(ui::SelectionMode::Extended);
    paste_.table = texts_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) {
        paste::Target tg;
        tg.noun = "texte";
        tg.nouns = "textes";
        tg.feminine = false;
        const auto& l = doc_->project.languages;
        tg.columns.push_back(paste::column("Texte (" + l.source() + ")", {"Texte", "Source", "Original", "Texte d'origine", l.source()}, 0, nullptr, true));
        for (std::size_t i = 1; i < l.list.size(); ++i) {
            const std::string code = l.list[i].code;
            const std::string name = l.list[i].name.empty() ? hmi::languageName(code) : l.list[i].name;
            tg.columns.push_back(paste::column(name + " (" + code + ")", {name, code, hmi::languageName(code)}, static_cast<int>(i),
                [this, code](const std::string& k, const std::string& v, std::string* why) { return setTranslation(k, code, v, why); }));
        }
        tg.columns.push_back(paste::column("O\xC3\xB9", {"Ou", "Utilise dans"}, static_cast<int>(l.list.size()), nullptr));
        tg.exists = [this](const std::string& k) {
            for (const auto& x : hmi::translatableTexts(doc_->project))
                if (x.source == k) return true;
            return false;
        };
        // Un texte qui n'est dans aucune vue ne se traduit pas : refuse (create nul).
        tg.unknown = "ce texte n'est dans aucune vue du projet";
        tg.keysFromAnchor = paste::keysFrom(*texts_, rq.anchorViewRow, 0);
        return tg;
    };
    paste::bind(paste_);
    links_ += doc_->changed->connect([this](Id) { refresh(); paste::forget(paste_); });
    refresh();
}

void HmiLanguagesPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiLanguagesPane::selectedLanguage() const {
    const auto rows = langs_->selectedModelRows();
    return rows.empty() || rows.front() >= langOrder_.size() ? std::string{} : langOrder_[rows.front()];
}

std::string HmiLanguagesPane::selectedText() const {
    const auto rows = texts_->selectedModelRows();
    return rows.empty() || rows.front() >= shownTexts_.size() ? std::string{} : shownTexts_[rows.front()].source;
}

void HmiLanguagesPane::selectLanguage(const std::string& code) {
    for (std::size_t i = 0; i < langOrder_.size(); ++i)
        if (same(langOrder_[i], code)) langs_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

void HmiLanguagesPane::selectText(const std::string& source) {
    bool found = false;
    for (std::size_t i = 0; i < shownTexts_.size(); ++i)
        if (shownTexts_[i].source == source) {
            texts_->selectModelRows({static_cast<ui::RowIndex>(i)});
            found = true;
        }
    if (!found && onlyMissing_) {
        // Un texte tout traduit : on le montre quand meme.
        onlyMissing_ = false;
        refreshTexts();
        for (std::size_t i = 0; i < shownTexts_.size(); ++i)
            if (shownTexts_[i].source == source) texts_->selectModelRows({static_cast<ui::RowIndex>(i)});
    }
    rebuildProperties();
}

void HmiLanguagesPane::setOnlyMissing(bool on) {
    onlyMissing_ = on;
    refreshTexts();
    rebuildProperties();
    invalidate();
}

void HmiLanguagesPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedLanguage();
    const auto& p = doc_->project;
    const auto& l = p.languages;
    const auto cov = hmi::coverage(p);
    const std::string start = !l.startLanguage.empty() && l.find(l.startLanguage) ? l.find(l.startLanguage)->code : l.source();
    langOrder_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> state;     // 0 la langue du projet, 1 complete, 2 incomplete
    for (std::size_t i = 0; i < l.list.size(); ++i) {
        const auto& lang = l.list[i];
        langOrder_.push_back(lang.code);
        std::string done = "(le projet)";
        int st = 0;
        if (i > 0)
            for (const auto& c : cov)
                if (same(c.code, lang.code)) {
                    done = std::to_string(c.translated) + " / " + std::to_string(c.total) + "  (" + percent(c.translated, c.total) + ")";
                    st = c.translated == c.total ? 1 : 2;
                }
        rows.push_back({upperCode(lang.code), lang.name.empty() ? hmi::languageName(lang.code) : lang.name, done,
                        same(lang.code, start) ? std::string("\xE2\x96\xB8 oui") : std::string{}});
        state.push_back(st);
    }
    langsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Code", "Langue", "Traduits", "D\xC3\xA9marrage"}, std::move(rows),
                                         [state](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle s;
                                             if (r >= state.size()) return s;
                                             if (c == 0) s.bold = true;
                                             if (c == 3) { s.bold = true; s.fgTone = ui::Tone::Accent; }
                                             if (c == 2) s.fgTone = state[r] == 0 ? ui::Tone::Muted : state[r] == 1 ? ui::Tone::Ok : ui::Tone::Warning;
                                             return s;
                                         });
    langs_->setModel(langsModel_);
    bool kept = false;
    for (std::size_t i = 0; i < langOrder_.size(); ++i)
        if (!keep.empty() && same(langOrder_[i], keep)) {
            langs_->selectModelRows({static_cast<ui::RowIndex>(i)});
            kept = true;
        }
    if (!kept && !langOrder_.empty()) langs_->selectModelRows({static_cast<ui::RowIndex>(langOrder_.size() > 1 ? 1 : 0)});
    refreshing_ = false;
    refreshTexts();
    rebuildProperties();
    std::size_t missing = 0, total = 0;
    for (const auto& c : cov) {
        missing += c.total - c.translated;
        total = c.total;
    }
    std::string msg = std::to_string(l.list.size()) + " langue(s) \xC2\xB7 " + std::to_string(total ? total : hmi::translatableTexts(p).size())
                    + " texte(s) \xC3\xA0 lire";
    if (l.list.size() > 1) msg += missing ? " \xC2\xB7 " + std::to_string(missing) + " traduction(s) manquante(s)" : std::string(" \xC2\xB7 tout est traduit");
    status_->setMessage(msg, missing ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiLanguagesPane::refreshTexts() {
    const bool was = refreshing_;
    refreshing_ = true;
    const std::string keep = selectedText();
    const auto& p = doc_->project;
    const auto& l = p.languages;
    const auto all = hmi::translatableTexts(p);
    shownTexts_.clear();
    std::vector<std::vector<std::string>> rows;
    // Par case : 0 rien, 1 manque (orange), 2 trous differents (rouge).
    std::vector<std::vector<int>> marks;
    for (const auto& t : all) {
        std::vector<std::string> row{t.source};
        std::vector<int> mark{0};
        bool missing = false;
        const auto holes = sortedHoles(t.source);
        for (std::size_t i = 1; i < l.list.size(); ++i) {
            const std::string tr = hmi::translationOf(l, l.list[i].code, t.source);
            int m = 0;
            if (tr.empty()) {
                m = 1;
                missing = true;
            } else if (sortedHoles(tr) != holes) {
                m = 2;
                missing = true;
            }
            row.push_back(tr);
            mark.push_back(m);
        }
        if (onlyMissing_ && !missing) continue;
        row.push_back(fewOf(t.uses, 2));
        mark.push_back(0);
        rows.push_back(std::move(row));
        marks.push_back(std::move(mark));
        shownTexts_.push_back(t);
    }
    std::vector<std::string> headers{"Texte (" + l.source() + ")"};
    std::vector<ui::TableView::Column> columns{{headers[0], 300.f}};
    for (std::size_t i = 1; i < l.list.size(); ++i) {
        headers.push_back((l.list[i].name.empty() ? hmi::languageName(l.list[i].code) : l.list[i].name) + " (" + l.list[i].code + ")");
        columns.push_back({headers.back(), 260.f});
    }
    headers.push_back("O\xC3\xB9");
    columns.push_back({headers.back(), 280.f});
    texts_->setColumns(columns);
    textsModel_ = std::make_shared<Rows>(std::move(headers), std::move(rows), [marks](ui::RowIndex r, std::size_t c) {
        ui::CellStyle s;
        if (r >= marks.size() || c >= marks[r].size()) return s;
        if (c == 0) s.bold = true;
        if (marks[r][c] == 1) s.bg = gfx::Color{242, 153, 74, 40};
        if (marks[r][c] == 2) {
            s.bg = gfx::Color{229, 83, 75, 60};
            s.icon = ui::Icon::Warning;
            s.iconTone = ui::Tone::Error;
        }
        if (c + 1 == marks[r].size()) s.fgTone = ui::Tone::Muted;
        return s;
    });
    texts_->setModel(textsModel_);
    for (std::size_t i = 0; i < shownTexts_.size(); ++i)
        if (!keep.empty() && shownTexts_[i].source == keep) texts_->selectModelRows({static_cast<ui::RowIndex>(i)});
    refreshing_ = was;
}

void HmiLanguagesPane::rebuildProperties() {
    const auto& p = doc_->project;
    const auto& l = p.languages;
    std::vector<PG::Category> cats;
    const std::string source = selectedText();
    if (!source.empty()) {
        PG::Category c;
        c.name = "Texte";
        c.properties.push_back(prop("Texte (" + l.source() + ")", source, PG::ValueType::ReadOnly, {},
                                    "Le texte tel qu'il est \xC3\xA9" "crit dans le projet. Ses trous {...} se remplissent en marche : "
                                    "une traduction les garde (le texte entre les accolades, et apr\xC3\xA8s les deux-points "
                                    "les mots d'un format \xC3\xA0 choix : {Marche:en marche|\xC3\xA0 l'arr\xC3\xAAt} \xE2\x86\x92 {Marche:running|stopped})."));
        const auto holes = sortedHoles(source);
        for (std::size_t i = 1; i < l.list.size(); ++i) {
            const std::string code = l.list[i].code;
            const std::string tr = hmi::translationOf(l, code, source);
            std::string help = "Vide : le texte reste en " + (l.list.front().name.empty() ? l.source() : l.list.front().name) + ".";
            if (!tr.empty() && sortedHoles(tr) != holes) help = "ATTENTION : cette traduction n'a pas les m\xC3\xAAmes trous {...} que le texte d'origine.";
            c.properties.push_back(prop((l.list[i].name.empty() ? hmi::languageName(code) : l.list[i].name) + " (" + code + ")", tr,
                                        PG::ValueType::Text,
                                        [this, source, code](std::string_view v) {
                                            message_.clear();
                                            return setTranslation(source, code, std::string(v));
                                        },
                                        help));
        }
        for (const auto& t : shownTexts_)
            if (t.source == source) c.properties.push_back(prop("O\xC3\xB9", joinList(t.uses), PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    const std::string code = selectedLanguage();
    if (const auto* lang = code.empty() ? nullptr : l.find(code)) {
        PG::Category c;
        c.name = "Langue";
        const bool first = same(lang->code, l.source());
        c.properties.push_back(prop("Code", lang->code, PG::ValueType::ReadOnly, {},
                                    first ? std::string("La langue dans laquelle le projet est \xC3\xA9" "crit.")
                                          : std::string("ISO 639-1 : en, de, es... Pour la changer, retirer la langue et en ajouter une autre.")));
        const std::string c2 = lang->code;
        c.properties.push_back(prop("Nom", lang->name, PG::ValueType::Text,
                                    [this, c2](std::string_view v) {
                                        message_.clear();
                                        return setLanguageName(c2, std::string(v));
                                    },
                                    "Ce que montre un s\xC3\xA9lecteur de langue (SYS.LanguageName) : le nom dans sa langue, English, Deutsch."));
        const std::string start = !l.startLanguage.empty() && l.find(l.startLanguage) ? l.find(l.startLanguage)->code : l.source();
        c.properties.push_back(prop("Au d\xC3\xA9marrage", tf(same(start, lang->code)), PG::ValueType::Boolean,
                                    [this, c2](std::string_view v) {
                                        message_.clear();
                                        return setStartLanguage(yes(std::string(v)) ? c2 : std::string{});
                                    },
                                    "L'IHM d\xC3\xA9marre dans cette langue (SYS.Language). Un s\xC3\xA9lecteur, l'action Changer de langue "
                                    "ou IHM_LANGUE('en') la changent en marche."));
        if (!first)
            for (const auto& cv : hmi::coverage(p))
                if (same(cv.code, lang->code))
                    c.properties.push_back(prop("Traduits", std::to_string(cv.translated) + " / " + std::to_string(cv.total), PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    if (cats.empty()) {
        PG::Category c;
        c.name = "Langues";
        c.properties.push_back(prop("Pour commencer", "Ajouter une langue (barre d'outils)", PG::ValueType::ReadOnly, {},
                                    "Le projet est \xC3\xA9" "crit dans une langue ; ajoute celles de l'op\xC3\xA9rateur, traduis les "
                                    "textes ici ou dans Excel, et pose un S\xC3\xA9lecteur de langue dans une vue."));
        cats.push_back(std::move(c));
    }
    grid_->setCategories(std::move(cats));
}

// ================================================================ les gestes ===
bool HmiLanguagesPane::change(const std::string& label, const std::function<void(hmi::Languages&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p.languages); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiLanguagesPane::addLanguage(const std::string& rawCode, const std::string& rawName, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    std::string code = trimmed(rawCode);
    // "English (en)", "English" : le code d'une langue connue.
    std::string parsedName, parsedCode;
    if (hmi::parseLanguageHeader(code, parsedName, parsedCode)) code = parsedCode;
    else
        for (const auto& k : hmi::knownLanguages())
            if (same(k.name, code)) code = k.code;
    for (auto& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!hmi::validLanguageCode(code)) return fail("code de langue \xC2\xAB " + rawCode + " \xC2\xBB : deux ou trois lettres (en, de, es), un sous-code facultatif (pt-br)");
    if (doc_->project.languages.find(code)) return fail("la langue " + code + " est d\xC3\xA9j\xC3\xA0 dans le projet");
    std::string name = trimmed(rawName);
    if (name.empty()) name = hmi::languageName(code);
    if (!change("Ajouter la langue " + name, [&](hmi::Languages& l) { l.list.push_back({code, name}); })) return fail("langue non ajout\xC3\xA9" "e");
    selectLanguage(code);
    say("Langue " + name + " (" + code + ") ajout\xC3\xA9" "e : traduis les textes ici, ou Exporter (Excel) pour un traducteur.");
    return true;
}

bool HmiLanguagesPane::removeLanguage(const std::string& code, std::string* why) {
    const auto& l = doc_->project.languages;
    const auto* lang = l.find(code);
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (!lang) return fail("langue inconnue : " + code);
    if (same(lang->code, l.source())) return fail("la premi\xC3\xA8re langue est celle du projet : elle reste");
    const std::string c = lang->code, name = lang->name;
    std::size_t dropped = 0;
    for (const auto& [source, byCode] : l.texts)
        for (const auto& [k, text] : byCode) dropped += same(k, c) && !text.empty();
    const bool ok = change("Retirer la langue " + name, [&](hmi::Languages& ls) {
        std::erase_if(ls.list, [&](const hmi::Language& x) { return same(x.code, c); });
        for (auto it = ls.texts.begin(); it != ls.texts.end();) {
            std::erase_if(it->second, [&](const auto& kv) { return same(kv.first, c); });
            it = it->second.empty() ? ls.texts.erase(it) : std::next(it);
        }
        if (same(ls.startLanguage, c)) ls.startLanguage.clear();
    });
    if (!ok) return fail("langue non retir\xC3\xA9" "e");
    say("Langue " + name + " retir\xC3\xA9" "e avec ses " + std::to_string(dropped) + " traduction(s) (Ctrl+Z les rend).");
    return true;
}

bool HmiLanguagesPane::setLanguageName(const std::string& code, const std::string& raw, std::string* why) {
    const auto* lang = doc_->project.languages.find(code);
    const std::string name = trimmed(raw);
    if (!lang || name.empty()) {
        const std::string m = !lang ? "langue inconnue : " + code : std::string("une langue a un nom");
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const std::string c = lang->code;
    return change("Langue " + c + " : nom", [&](hmi::Languages& l) {
        for (auto& x : l.list)
            if (same(x.code, c)) x.name = name;
    });
}

bool HmiLanguagesPane::setStartLanguage(const std::string& code, std::string* why) {
    const auto& l = doc_->project.languages;
    std::string c;
    if (!code.empty()) {
        const auto* lang = l.find(code);
        if (!lang) {
            const std::string m = "langue inconnue : " + code;
            say(m, true);
            if (why) *why = m;
            return false;
        }
        c = same(lang->code, l.source()) ? std::string{} : lang->code;     // la premiere : vide
    }
    if (l.startLanguage == c) return true;
    if (!change("Langue de d\xC3\xA9marrage", [&](hmi::Languages& ls) { ls.startLanguage = c; })) return false;
    say("Au lancement, l'IHM sera en " + hmi::languageName(c.empty() ? l.source() : c) + ".");
    return true;
}

bool HmiLanguagesPane::setTranslation(const std::string& source, const std::string& code, const std::string& raw, std::string* why) {
    const auto& l = doc_->project.languages;
    const auto* lang = l.find(code);
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (!lang) return fail("langue inconnue : " + code);
    if (same(lang->code, l.source())) return fail("la langue du projet ne se traduit pas : c'est le texte lui-m\xC3\xAA" "me");
    const std::string key = trimmed(source);
    if (key.empty()) return fail("pas de texte");
    const std::string text = trimmed(raw);
    const std::string c = lang->code;
    if (hmi::translationOf(l, c, key) == text) return true;
    const bool ok = change("Traduction (" + c + ") : " + key.substr(0, 40), [&](hmi::Languages& ls) {
        if (text.empty()) {
            if (auto it = ls.texts.find(key); it != ls.texts.end()) {
                std::erase_if(it->second, [&](const auto& kv) { return same(kv.first, c); });
                if (it->second.empty()) ls.texts.erase(it);
            }
            return;
        }
        auto& byCode = ls.texts[key];
        std::erase_if(byCode, [&](const auto& kv) { return same(kv.first, c) && kv.first != c; });
        byCode[c] = text;
    });
    if (!ok) return fail("traduction non enregistr\xC3\xA9" "e");
    selectText(key);
    if (!text.empty() && sortedHoles(text) != sortedHoles(key))
        say("Traduction enregistr\xC3\xA9" "e, mais ses trous {...} ne sont pas ceux du texte : elle ne montrera pas la m\xC3\xAAme chose.", true);
    return true;
}

// ================================================================ Excel ======
bool HmiLanguagesPane::exportTranslations(std::string* where) {
    const auto& p = doc_->project;
    auto table = hmi::translationTable(p);
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName("traductions_" + fileSafe(p.config.name) + "_" + hmi::wallStamp().substr(0, 10), hmi::ExportFormat::Excel);
    rq.format = std::string(hmi::exportFormatLabel(hmi::ExportFormat::Excel));
    rq.source = "traductions";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, hmi::ExportFormat::Excel));
    rq.origin = "Configuration > Langues";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? std::to_string(table.rows.size()) + " texte(s) export\xC3\xA9s : " + path + " - remplis les colonnes des langues, puis Importer."
           : "Export impossible" + (path.empty() ? std::string{} : " : " + path),
        !ok);
    return ok;
}

bool HmiLanguagesPane::importFile(const std::string& path, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    xls::ReadOptions o;
    o.table.descriptionRows = 0;
    o.table.anchors = {"Texte (" + doc_->project.languages.source() + ")"};
    auto r = xls::read(path, o);
    if (!r.ok || r.table.columnCount() == 0) {
        // Un classeur fait a la main : l'en-tete devine.
        o.table.anchors.clear();
        r = xls::read(path, o);
    }
    if (!r.ok || r.table.columnCount() == 0) return fail("classeur illisible : " + path + (r.warnings.empty() ? std::string{} : " (" + r.warnings.front() + ")"));
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 0; i < r.table.rowCount(); ++i) {
        std::vector<std::string> row;
        for (std::size_t c = 0; c < r.table.columnCount(); ++c) row.push_back(r.table.cell(i, c));
        rows.push_back(std::move(row));
    }
    return importTable(r.table.headers(), rows, why);
}

bool HmiLanguagesPane::importTable(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows,
                                   std::string* why) {
    hmi::Languages next = doc_->project.languages;
    lastImport_ = hmi::importTranslations(doc_->project, next, headers, rows);
    const auto& im = lastImport_;
    if (next == doc_->project.languages) {
        const std::string m = "Rien de nouveau dans le classeur" + (im.warnings.empty() ? std::string{} : " : " + im.warnings.front());
        say(m, !im.warnings.empty());
        if (why) *why = m;
        return im.warnings.empty();
    }
    if (!change("Importer les traductions", [&](hmi::Languages& l) { l = next; })) {
        say("import non enregistr\xC3\xA9", true);
        if (why) *why = "import non enregistr\xC3\xA9";
        return false;
    }
    std::string m = std::to_string(im.updated) + " traduction(s) pos\xC3\xA9" "e(s)";
    if (im.cleared) m += ", " + std::to_string(im.cleared) + " retir\xC3\xA9" "e(s)";
    if (!im.languagesAdded.empty()) m += " ; langue(s) ajout\xC3\xA9" "e(s) : " + joinList(im.languagesAdded, ", ");
    if (im.unknown) m += " ; " + std::to_string(im.unknown) + " texte(s) qui ne sont plus dans le projet (gard\xC3\xA9s)";
    if (!im.warnings.empty()) m += " ; " + std::to_string(im.warnings.size()) + " avertissement(s) : " + im.warnings.front();
    say(m + " (Ctrl+Z annule l'import).", !im.warnings.empty());
    if (why) *why = m;
    return true;
}

void HmiLanguagesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float listW = std::min(440.f, b.w * 0.27f);
    const float gridW = std::min(420.f, b.w * 0.26f);
    langs_->setBounds({b.x, b.y + 38, listW, h});
    texts_->setBounds({b.x + listW + 4, b.y + 38, std::max(0.f, b.w - listW - gridW - 8), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiLanguagesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// =============================================================================
//  Configuration > Unites et formats
// =============================================================================
namespace {
enum : int { UAdd = 1, URemove, UPropose };
}

HmiUnitsPane::HmiUnitsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(UAdd, HmiGlyph::Plus, "Ajouter une variable : son unit\xC3\xA9 et son format", "Ajouter");
    tools->add(URemove, HmiGlyph::Delete, "Retirer la variable choisie (ses objets reprennent leurs unit\xC3\xA9s et formats)", "Retirer");
    tools->separator();
    tools->add(UPropose, HmiGlyph::Search, "Proposer : chaque variable montr\xC3\xA9" "e avec une unit\xC3\xA9 ou un format par un objet des vues", "Proposer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(URemove, [this] { return !selectedDisplay().empty(); });
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Variable", 280.f}, {"Unit\xC3\xA9", 80.f}, {"Format", 110.f}, {"Exemple", 150.f}, {"Montr\xC3\xA9" "e par", 470.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case UAdd:
                if (hosts_.askAdd) hosts_.askAdd();
                else say("Ajouter : la variable est demand\xC3\xA9" "e par l'application.", true);
                break;
            case URemove: if (!selectedDisplay().empty()) (void)removeDisplay(selectedDisplay()); break;
            case UPropose: (void)proposeFromViews(); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    // Lot 20 : coller des unites et des formats depuis Excel (la variable dit
    // quelle ligne ; une variable sans ligne en recoit une).
    table_->setSelectionMode(ui::SelectionMode::Extended);
    paste_.table = table_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) {
        paste::Target tg;
        tg.noun = "ligne";
        tg.nouns = "lignes";
        tg.columns.push_back(paste::column("Variable", {"Nom", "Chemin", "Name", "Tag"}, 0, nullptr, true));
        tg.columns.push_back(paste::column("Unit\xC3\xA9", {"Unit", "Unite de mesure"}, 1,
            [this](const std::string& k, const std::string& v, std::string* why) { return setField(k, "unite", v, why); }));
        tg.columns.push_back(paste::column("Format", {"Decimales", "Affichage"}, 2,
            [this](const std::string& k, const std::string& v, std::string* why) { return setField(k, "format", v, why); }));
        tg.columns.push_back(paste::column("Exemple", {}, 3, nullptr));
        tg.columns.push_back(paste::column("Montr\xC3\xA9" "e par", {"Montree par"}, 4, nullptr));
        tg.exists = [this](const std::string& k) {
            for (const auto& d : doc_->project.displays)
                if (same(d.path, k)) return true;
            return false;
        };
        tg.create = [this](const std::string& k, const std::map<std::string, std::string>& cells, paste::Notes& notes,
                           std::vector<std::string>& used, std::string* why) -> std::string {
            const auto unit = cells.find("unite");
            const auto format = cells.find("format");
            std::string f = format == cells.end() ? std::string{} : format->second;
            if (!f.empty() && !hmi::looksLikeFormat(f)) {
                notes.push_back({"Format", "format \xC2\xAB " + f + " \xC2\xBB illisible (0, 0.0, 0.00, 000, 0.0%)"});
                f.clear();
            }
            if (!addDisplay(k, unit == cells.end() ? std::string{} : unit->second, f, why)) return {};
            used.push_back("unite");
            used.push_back("format");
            return trimmed(k);
        };
        tg.keysFromAnchor = paste::keysFrom(*table_, rq.anchorViewRow, 0);
        return tg;
    };
    paste::bind(paste_);
    links_ += doc_->changed->connect([this](Id) { refresh(); paste::forget(paste_); });
    refresh();
}

void HmiUnitsPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiUnitsPane::selectedDisplay() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? std::string{} : order_[rows.front()];
}

void HmiUnitsPane::selectDisplay(const std::string& path) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], path)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

void HmiUnitsPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedDisplay();
    const auto& p = doc_->project;
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> unused;
    for (const auto& d : p.displays) {
        order_.push_back(d.path);
        const auto uses = hmi::displayUses(p, d);
        rows.push_back({d.path, d.unit, d.format.empty() ? std::string("(de l'objet)") : d.format, hmi::displaySample(d),
                        uses.empty() ? std::string("(aucun objet)") : std::to_string(uses.size()) + " : " + fewOf(uses, 3)});
        unused.push_back(uses.empty());
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Variable", "Unit\xC3\xA9", "Format", "Exemple", "Montr\xC3\xA9" "e par"}, std::move(rows),
                                    [unused](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle st;
                                        if (r >= unused.size()) return st;
                                        if (c == 0) { st.bold = true; st.icon = ui::Icon::Variable; st.iconTone = unused[r] ? ui::Tone::Muted : ui::Tone::Ok; }
                                        if (c == 3) st.fgTone = ui::Tone::Accent;
                                        if (c == 4 && unused[r]) st.fgTone = ui::Tone::Muted;
                                        return st;
                                    });
    table_->setModel(model_);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (!keep.empty() && same(order_[i], keep)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    if (selectedDisplay().empty() && !order_.empty()) table_->selectModelRows({0});
    refreshing_ = false;
    rebuildProperties();
    status_->setMessage(std::to_string(p.displays.size()) + " variable(s) avec une unit\xC3\xA9 ou un format", ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiUnitsPane::rebuildProperties() {
    const auto& p = doc_->project;
    const std::string path = selectedDisplay();
    const hmi::VariableDisplay* d = nullptr;
    for (const auto& x : p.displays)
        if (same(x.path, path)) d = &x;
    if (!d) {
        PG::Category c;
        c.name = "Unit\xC3\xA9s et formats";
        c.properties.push_back(prop("Pour commencer", "Ajouter, ou Proposer (barre d'outils)", PG::ValueType::ReadOnly, {},
                                    "Une variable, son unit\xC3\xA9 (bar, \xC2\xB0" "C, %) et son format (0.0) : repris par chaque "
                                    "objet qui la montre (afficheur, champ de saisie, jauge, bargraphe, tableau de variables...) et par "
                                    "les trous des textes ({Pression} : son format ; {Pression:u} : son format et son unit\xC3\xA9)."));
        grid_->setCategories({std::move(c)});
        return;
    }
    const std::string key = d->path;
    const auto commit = [this, key](const char* field) {
        return [this, key, field](std::string_view v) {
            message_.clear();
            return setField(key, field, std::string(v));
        };
    };
    PG::Category c;
    c.name = "Variable";
    c.properties.push_back(prop("Variable", d->path, PG::ValueType::Text, commit("variable"),
                                "Une variable IHM ou de l'automate. Armoires[].ana.PT1.mes vaut pour chaque case du tableau ; "
                                "un chemin exact (Armoires[2].ana.PT1.mes) l'emporte."));
    c.properties.push_back(prop("Unit\xC3\xA9", d->unit, PG::ValueType::Text, commit("unite"),
                                "bar, \xC2\xB0" "C, %, m\xC2\xB3/h... Vide : chaque objet garde la sienne."));
    c.properties.push_back(prop("Format", d->format, PG::ValueType::Enum, commit("format"),
                                "0 : entier ; 0.0, 0.00 : une, deux d\xC3\xA9" "cimales ; 000 : trois chiffres au moins ; 0.0% : en pour cent. "
                                "Vide : chaque objet garde le sien.",
                                {"", "0", "0.0", "0.00", "0.000", "000", "0.0%"}));
    c.properties.push_back(prop("Exemple", hmi::displaySample(*d), PG::ValueType::ReadOnly));
    const auto uses = hmi::displayUses(p, *d);
    c.properties.push_back(prop("Montr\xC3\xA9" "e par", uses.empty() ? std::string("aucun objet") : joinList(uses), PG::ValueType::ReadOnly));
    grid_->setCategories({std::move(c)});
}

bool HmiUnitsPane::addDisplay(const std::string& rawPath, const std::string& unit, const std::string& format, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string path = trimmed(rawPath);
    if (!hmi::validDisplayPath(path)) return fail("variable \xC2\xAB " + rawPath + " \xC2\xBB : un chemin (Pression, Armoires[].ana.PT1.mes)");
    for (const auto& d : doc_->project.displays)
        if (same(d.path, path)) return fail(path + " a d\xC3\xA9j\xC3\xA0 sa ligne");
    const std::string f = trimmed(format);
    if (!f.empty() && !hmi::looksLikeFormat(f)) return fail("format \xC2\xAB " + f + " \xC2\xBB illisible (0, 0.0, 0.00, 000, 0.0%)");
    auto cmd = hmi::changeProject(doc_, "Unit\xC3\xA9 de " + path, [&](hmi::Project& pr) { pr.displays.push_back({path, trimmed(unit), f}); });
    if (!cmd) return fail("non ajout\xC3\xA9" "e");
    apply_(std::move(cmd));
    refresh();
    selectDisplay(path);
    say(path + " : " + hmi::displaySample({path, trimmed(unit), f}) + " partout o\xC3\xB9 elle s'affiche.");
    return true;
}

bool HmiUnitsPane::removeDisplay(const std::string& path, std::string* why) {
    bool found = false;
    for (const auto& d : doc_->project.displays) found = found || same(d.path, path);
    if (!found) {
        const std::string m = "variable sans ligne : " + path;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    auto cmd = hmi::changeProject(doc_, "Retirer l'unit\xC3\xA9 de " + path, [&](hmi::Project& pr) {
        std::erase_if(pr.displays, [&](const hmi::VariableDisplay& d) { return same(d.path, path); });
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    say(path + " retir\xC3\xA9" "e (Ctrl+Z la rend) : ses objets reprennent leurs unit\xC3\xA9s et formats.");
    return true;
}

bool HmiUnitsPane::setField(const std::string& path, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const hmi::VariableDisplay* cur = nullptr;
    for (const auto& d : doc_->project.displays)
        if (same(d.path, path)) cur = &d;
    if (!cur) return fail("variable sans ligne : " + path);
    const std::string value = trimmed(raw);
    if (key == "variable") {
        if (!hmi::validDisplayPath(value)) return fail("variable \xC2\xAB " + raw + " \xC2\xBB : un chemin (Pression, Armoires[].ana.PT1.mes)");
        for (const auto& d : doc_->project.displays)
            if (&d != cur && same(d.path, value)) return fail(value + " a d\xC3\xA9j\xC3\xA0 sa ligne");
    } else if (key == "format") {
        if (!value.empty() && !hmi::looksLikeFormat(value)) return fail("format \xC2\xAB " + value + " \xC2\xBB illisible (0, 0.0, 0.00, 000, 0.0%)");
    } else if (key != "unite") {
        return fail("champ inconnu : " + key);
    }
    const std::string old = cur->path;
    auto cmd = hmi::changeProject(doc_, "Unit\xC3\xA9s et formats : " + old, [&](hmi::Project& pr) {
        for (auto& d : pr.displays)
            if (same(d.path, old)) (key == "variable" ? d.path : key == "format" ? d.format : d.unit) = value;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectDisplay(key == "variable" ? value : old);
    return true;
}

std::size_t HmiUnitsPane::proposeFromViews() {
    const auto& p = doc_->project;
    std::vector<hmi::VariableDisplay> found;
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            if (o.kind == hmi::Kind::VariableTable) continue;
            if (!o.find("unit") && !o.find("format")) continue;
            const std::string var = hmi::displayVariableOf(o);
            if (var.empty() || !hmi::validDisplayPath(var) || hmi::displayOf(p, var, &v)) continue;
            const std::string unit = trimmed(o.text("unit")), format = trimmed(o.text("format"));
            if (unit.empty()) continue;                                  // un format seul : trop peu pour une regle
            // Une ligne par variable : Armoires[0].x et Armoires[1].x -> Armoires[].x ;
            // dans une popup, son parametre remplace par ce qu'il designe.
            const std::string path = hmi::displayRowPath(var, &v);
            if (!hmi::validDisplayPath(path)) continue;
            bool known = false;
            for (const auto& f : found) known = known || same(f.path, path);
            if (!known) found.push_back({path, unit, hmi::looksLikeFormat(format) ? format : std::string{}});
        }
    if (found.empty()) {
        say("Rien \xC3\xA0 proposer : chaque variable montr\xC3\xA9" "e avec une unit\xC3\xA9 a d\xC3\xA9j\xC3\xA0 sa ligne.");
        return 0;
    }
    auto cmd = hmi::changeProject(doc_, "Proposer les unit\xC3\xA9s", [&](hmi::Project& pr) {
        for (auto& f : found) pr.displays.push_back(f);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say(std::to_string(found.size()) + " variable(s) ajout\xC3\xA9" "e(s), avec l'unit\xC3\xA9 et le format de l'objet qui les montre (Ctrl+Z les retire).");
    return found.size();
}

void HmiUnitsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(420.f, b.w * 0.3f);
    table_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiUnitsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
