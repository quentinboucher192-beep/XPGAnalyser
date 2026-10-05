#include "LibraryHelpScreen.hpp"

#include "../../menu/MenuManager.hpp"
#include "../../project/MacroFolders.hpp"
#include "../../ui/widgets/PathBrowse.hpp"     // Exporter le dossier d'aide : l'explorateur

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace app {
namespace {

// =============================================================================
//  LA LISTE " GARDEES ET RECENTES "
// -----------------------------------------------------------------------------
//  Un modele de liste tout bete : le texte est calcule d'avance par l'ecran,
//  qui est le seul a savoir ce qu'est une destination. Chaque ligne porte son
//  icone et sa couleur - l'etoile ambre des favoris, l'icone du genre dans la
//  teinte de sa famille pour les recents - pour qu'on la reconnaisse avant de
//  la lire.
// =============================================================================
class PlacesListModel final : public ui::IListModel {
public:
    struct Row {
        std::string text;
        ui::Icon    icon{ui::Icon::None};
        ui::Tone    tone{ui::Tone::None};
        bool        header{false};
        bool        muted{false};
    };
    void setRows(std::vector<Row> rows) { rows_ = std::move(rows); }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::string text(ui::RowIndex r) const override {
        return r < rows_.size() ? rows_[r].text : std::string{};
    }
    [[nodiscard]] ui::CellStyle style(ui::RowIndex r) const override {
        ui::CellStyle s;
        if (r >= rows_.size()) return s;
        const auto& row = rows_[r];
        s.icon     = row.icon;
        s.iconTone = row.tone;
        s.bold     = row.header;
        if (row.header) s.fgTone = ui::Tone::Accent;
        if (row.muted)  s.fgTone = ui::Tone::Muted;
        return s;
    }
private:
    std::vector<Row> rows_;
};

ui::WidgetPtr makeArticle(ui::HelpArticleView*& out) {
    auto v = std::make_unique<ui::HelpArticleView>("help.macros.article");
    out = v.get();
    return v;
}

// Un titre + un paragraphe, la forme minimale d'un article fabrique ici.
void addBlock(ui::HelpArticle& a, ui::HelpBlockKind kind, std::string text,
              std::string label = {}, std::string detail = {}) {
    ui::HelpBlock b;
    b.kind   = kind;
    b.text   = std::move(text);
    b.label  = std::move(label);
    b.detail = std::move(detail);
    a.blocks.push_back(std::move(b));
}

std::string_view kindWord(help::HitKind k) {
    switch (k) {
        case help::HitKind::Name:       return "nom";
        case help::HitKind::Param:      return "param\xC3\xA8tre";
        case help::HitKind::Fault:      return "code de d\xC3\xA9" "faut";
        case help::HitKind::Question:   return "question de macro";
        case help::HitKind::Summary:    return "r\xC3\xA9sum\xC3\xA9";
        case help::HitKind::Usage:      return "utilisation";
        case help::HitKind::Example:    return "exemple";
        case help::HitKind::Diagnostic: return "diagnostic";
        case help::HitKind::Glossary:   return "glossaire";
    }
    return {};
}

ui::Icon kindIcon(project::CatalogKind k) {
    return k == project::CatalogKind::FunctionBlock ? ui::Icon::FunctionBlock
         : k == project::CatalogKind::DerivedType   ? ui::Icon::DerivedType
                                                    : ui::Icon::Document;
}

// Ou ecrire quand l'hote n'a pas pose de selecteur de fichier. Le dossier
// temporaire de l'utilisateur : il existe, il est inscriptible, et il est le
// meme d'une fois sur l'autre - ce qui permet de retrouver le fichier sans le
// chercher.
std::string defaultExportDir() {
    for (const char* var : {"TEMP", "TMP", "TMPDIR"})
        if (const char* v = std::getenv(var); v != nullptr && *v != '\0') return v;
    return ".";
}

std::unique_ptr<PillButton> pill(std::string text, ui::Icon icon, PillButton::Kind kind,
                                 std::string tip, std::string id) {
    auto b = std::make_unique<PillButton>(std::move(text), icon, kind, std::move(id));
    b->setTooltip(std::move(tip));
    return b;
}

} // namespace

LibraryHelpPage::LibraryHelpPage(std::string libsRoot, std::string id, HelpScope scope)
    : ui::DockLayout(std::move(id)), libsRoot_(std::move(libsRoot)), scope_(scope) {
    buildUi();
    reload();
}

std::string LibraryHelpPage::rootLabel() const {
    switch (scope_) {
        case HelpScope::Macros: return "Macros";
        case HelpScope::Blocks: return "Blocs DFB et DDT";
        case HelpScope::All:    break;
    }
    return {};
}

const project::macro::MacroSpec* LibraryHelpPage::macroSpec(const std::string& name) const {
    const auto it = specs_.find(name);
    return it == specs_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
void LibraryHelpPage::buildUi() {
    // --- la barre -------------------------------------------------------------
    //
    //  QUATRE GROUPES, DANS L'ORDRE DES GESTES. Naviguer (Precedent, Suivant,
    //  Garder), essayer (l'action principale, pleine, seule de son espece),
    //  ecrire (Enregistrer / Annuler, qui n'apparaissent que quand il y a
    //  quelque chose a enregistrer), et a droite ce qui sort de la page :
    //  chercher partout, les diagnostics, imprimer, le dossier.
    {
        auto bar = std::make_unique<HelpBar>("help.macros.toolbar");
        toolbar_ = bar.get();
        using K = PillButton::Kind;

        auto back = pill("", ui::Icon::None, K::Ghost, "Page pr\xC3\xA9" "c\xC3\xA9" "dente (Alt+Gauche)",
                         "help.macros.back");
        back->setGlyph(PillButton::Glyph::Back);
        back_ = &toolbar_->add(std::move(back));
        auto fwd = pill("", ui::Icon::None, K::Ghost, "Page suivante (Alt+Droite)",
                        "help.macros.forward");
        fwd->setGlyph(PillButton::Glyph::Forward);
        forward_ = &toolbar_->add(std::move(fwd));
        star_ = &toolbar_->add(pill("Garder", ui::Icon::Star, K::Ghost,
                                    "Garder cette page dans les favoris", "help.macros.star"));
        toolbar_->addSeparator();

        tryIt_ = &toolbar_->add(pill("Essayer", ui::Icon::Play, K::Primary,
                                     "Faire tourner l'exemple dans le simulateur (F5)",
                                     "help.macros.try"));
        toolbar_->addSeparator();

        save_ = &toolbar_->add(pill("Enregistrer dans libs", ui::Icon::Save, K::Subtle,
                                    "\xC3\x89" "crit l'aide dans le fichier de la biblioth\xC3\xA8que",
                                    "help.macros.save"));
        save_->setDot(ui::Tone::Warning);
        revert_ = &toolbar_->add(pill("Annuler", ui::Icon::Close, K::Ghost,
                                      "Abandonne les modifications ; le fichier n'est pas touch\xC3\xA9",
                                      "help.macros.revert"));

        toolbar_->addSpacer();
        toolbar_->add(pill("Aller \xC3\xA0", ui::Icon::Search, K::Subtle,
                           "Un bloc, un dossier, une commande : tapez trois lettres (Ctrl+F)",
                           "help.macros.goto"));
        toolbar_->add(pill("Diagnostics", ui::Icon::Info, K::Ghost,
                           "Les codes XPG-nnnn et ce qu'ils veulent dire (F1)", "help.macros.diag"));
        toolbar_->add(pill("Imprimer", ui::Icon::Print, K::Ghost,
                           "Cette page, pr\xC3\xAAte \xC3\xA0 imprimer depuis le navigateur (Ctrl+P)",
                           "help.macros.print"));
        toolbar_->add(pill("Dossier d'aide", ui::Icon::Export, K::Ghost,
                           "Toute la biblioth\xC3\xA8que en un document", "help.macros.export"));
        toolbar_->add(pill("", ui::Icon::Refresh, K::Ghost, "Relire libs/ depuis le disque",
                           "help.macros.reload"));

        // Les clics, par identifiant : un seul endroit dit ce que fait chaque
        // bouton, et c'est le meme chemin que les raccourcis.
        for (const auto& child : toolbar_->children()) {
            auto* b = dynamic_cast<PillButton*>(child.get());
            if (b == nullptr) continue;
            const std::string id = b->id();
            links_ += b->clicked->connect([this, id] {
                if      (id == "help.macros.back")    runCommand(CmdBack);
                else if (id == "help.macros.forward") runCommand(CmdForward);
                else if (id == "help.macros.star")    runCommand(CmdFavourite);
                else if (id == "help.macros.try")     runCommand(CmdTry);
                else if (id == "help.macros.save")    save();
                else if (id == "help.macros.revert")  revert();
                else if (id == "help.macros.goto")    openPalette();
                else if (id == "help.macros.diag")    runCommand(CmdDiagnostics);
                else if (id == "help.macros.print")   runCommand(CmdPrintPage);
                else if (id == "help.macros.export")  runCommand(CmdExportDossier);
                else if (id == "help.macros.reload")  reload();
            });
        }
        dock(std::move(bar), Side::Top, 48.f);
    }

    // --- le bandeau d'etat --------------------------------------------------
    {
        auto st = std::make_unique<ui::StatusBar>("help.macros.status");
        status_ = st.get();
        dock(std::move(st), Side::Bottom, 26.f);
    }

    // --- le centre : arbre | onglets ---------------------------------------
    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal,
                                                "help.macros.split");

    // gauche : filtre au-dessus de l'arbre
    {
        auto left = std::make_unique<ui::BoxLayout>(ui::Orientation::Vertical,
                                                    "help.macros.left");
        auto f = std::make_unique<ui::InputText>("help.macros.filter");
        f->setPlaceholder("filtrer ; Entr\xC3\xA9" "e cherche partout ; Ctrl+F pour aller n'importe o\xC3\xB9");
        filter_ = f.get();
        links_ += filter_->textChanged->connect(
            [this](const std::string& t) { applyFilter(t); });
        // ENTREE NE FILTRE PAS, ELLE CHERCHE. Le filtre ne voit que ce que
        // l'arbre affiche - un nom, une categorie, un resume. La recherche voit
        // les parametres, les codes de defaut, les questions des macros, les
        // codes XPG-nnnn et le glossaire, et elle CLASSE. Les deux gestes ont
        // l'air du meme, et repondent a deux questions differentes.
        links_ += filter_->editingDone->connect(
            [this](const std::string& t) { runSearch(t); });
        left->addChild(std::move(f));

        auto t = std::make_unique<ui::TreeView>("help.macros.tree");
        tree_ = t.get();
        tree_->setShowRootNode(true);
        // UN DOSSIER S'OUVRE AUSSI. Cliquer " Equipements " ne faisait rien :
        // l'article restait celui d'avant, et on croyait le clic perdu. Il
        // montre maintenant la page du dossier - ce qu'il contient, et ou en est
        // sa documentation.
        const auto onNode = [this](ui::NodeId n) {
            if (treeModel_) {
                if (const auto folder = treeModel_->folderOf(n); folder.has_value()) {
                    showCategory(*folder);
                    return;
                }
            }
            goTo(targetForNode(n));
        };
        links_ += tree_->selectionChanged->connect(onNode);
        links_ += tree_->activated->connect(onNode);
        left->addChild(std::move(t));

        // CE QU'ON A GARDE, ET CE QU'ON VIENT DE LIRE. Les favoris et
        // l'historique existaient dans le modele et n'etaient affiches nulle
        // part : une etoile qui ne se voit pas est une etoile qui ne sert a
        // rien, et un historique sans liste n'est qu'un bouton Precedent.
        auto box = std::make_unique<ui::GroupBox>("Gard\xC3\xA9" "es et r\xC3\xA9" "centes",
                                                  "help.macros.placesbox");
        auto liste = std::make_unique<ui::ListView>("help.macros.places");
        places_ = liste.get();
        placesModel_ = std::make_shared<PlacesListModel>();
        places_->setModel(placesModel_);
        const auto onPlace = [this](ui::RowIndex r) {
            if (r < placeTargets_.size() && placeTargets_[r].kind != help::TargetKind::None)
                goTo(placeTargets_[r]);
        };
        // UN CLIC SUFFIT. Il fallait un double-clic : dans une liste de liens,
        // personne ne le devine, et un lien qu'on clique sans effet passe pour
        // mort.
        links_ += places_->selectionChanged->connect(onPlace);
        links_ += places_->activated->connect(onPlace);
        box->body().addChild(std::move(liste));
        left->addChild(std::move(box));

        split->addPane(std::move(left), 0.30f, 220.f);
    }

    // droite : les quatre onglets
    {
        auto tabs = std::make_unique<ui::TabControl>("help.macros.tabs");
        tabs_ = tabs.get();

        tabs_->addTab({"Aide", ui::Icon::Document, false, false}, makeArticle(article_));
        // Les liens de l'article : fil d'Ariane, puces, types cliquables, bouton
        // Copier. Le widget ne sait pas ce qu'ils designent ; la page, si.
        links_ += article_->linkActivated->connect([this](const std::string& t) { onLink(t); });

        // --- Modifier -------------------------------------------------------
        {
            auto edit = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal,
                                                       "help.macros.edit");
            auto list = std::make_unique<ui::ListView>("help.macros.fields");
            fieldList_ = list.get();
            links_ += fieldList_->selectionChanged->connect(
                [this](ui::RowIndex r) { selectField(r); });
            links_ += fieldList_->activated->connect(
                [this](ui::RowIndex r) { selectField(r); });
            edit->addPane(std::move(list), 0.30f, 160.f);

            auto right = std::make_unique<ui::Splitter>(ui::Orientation::Vertical,
                                                        "help.macros.editright");
            {
                auto box = std::make_unique<ui::GroupBox>("R\xC3\xA9sum\xC3\xA9", "help.macros.editbox");
                editorBox_ = box.get();
                auto text = std::make_unique<ui::MultiLineText>("help.macros.editor");
                editor_ = text.get();
                editor_->setReadOnly(false);
                editor_->setWordWrap(true);
                editor_->setShowLineNumbers(false);
                editor_->setLanguage(ui::Language::PlainText);
                links_ += editor_->textChanged->connect(
                    [this](const std::string& t) { onDraftEdited(t); });
                editorBox_->body().addChild(std::move(text));
                right->addPane(std::move(box), 0.62f, 120.f);
            }
            {
                // L'apercu montre les octets qui partiront dans le fichier. On
                // documente dans des commentaires d'un format precis ; voir le
                // resultat avant d'enregistrer est ce qui evite de decouvrir
                // apres coup qu'une ligne est devenue un parametre.
                auto box = std::make_unique<ui::GroupBox>(
                    "Ce qui sera \xC3\xA9" "crit dans le fichier", "help.macros.previewbox");
                previewBox_ = box.get();
                auto text = std::make_unique<ui::MultiLineText>("help.macros.preview");
                preview_ = text.get();
                preview_->setReadOnly(true);
                preview_->setShowLineNumbers(false);
                preview_->setWordWrap(false);
                previewBox_->body().addChild(std::move(text));
                right->addPane(std::move(box), 0.38f, 90.f);
            }
            edit->addPane(std::move(right), 0.70f, 260.f);
            tabs_->addTab({"Modifier", ui::Icon::Settings, false, false}, std::move(edit));
        }

        // --- Source ----------------------------------------------------------
        {
            auto src = std::make_unique<ui::MultiLineText>("help.macros.source");
            source_ = src.get();
            source_->setReadOnly(true);
            source_->setShowLineNumbers(true);
            source_->setLanguage(ui::Language::StructuredText);
            tabs_->addTab({"Source", ui::Icon::Section, false, false}, std::move(src));
        }

        // --- Essai ------------------------------------------------------------
        //
        //  UN ONGLET, ET TOUT LE PROBLEME DISPARAIT. La premiere version
        //  remplacait l'article par le releve : on ne savait plus ou on etait
        //  ni comment revenir, et un ecran dont on ne sait pas sortir passe
        //  pour plante. Un onglet se quitte comme les trois autres.
        {
            auto pane = std::make_unique<BenchPane>("help.macros.bench");
            benchPane_ = pane.get();
            links_ += benchPane_->closed->connect([this] {
                session_.reset();
                benchPane_->setBench(nullptr, nullptr);
                if (tabs_) {
                    tabs_->setCurrentIndex(0);
                    tabs_->setTabLive(benchTab_, false);
                }
                setStatus("essai ferm\xC3\xA9 ; le projet de poche est jet\xC3\xA9");
                refreshCommands();
            });
            links_ += benchPane_->message->connect([this](const std::string& m, bool error) {
                setStatus(m, error);
            });
            // L'onglet dit que ca tourne, meme quand on est sur un autre : une
            // LED qui respire a cote de " Essai ".
            links_ += benchPane_->stateChanged->connect([this](help::TryBench::State st) {
                if (tabs_) tabs_->setTabLive(benchTab_, st == help::TryBench::State::Running);
            });
            benchTab_ = tabs_->tabCount();
            tabs_->addTab({"Essai", ui::Icon::Play, false, false}, std::move(pane));
        }

        split->addPane(std::move(tabs), 0.70f, 320.f);
    }

    dock(std::move(split), Side::Center, 0.f);

    // --- la palette : au-dessus de tout, et fermee -------------------------------
    //  Ajoutee EN DERNIER : c'est ce qui la peint par-dessus les autres et lui
    //  donne les evenements en premier quand elle est ouverte. Elle n'est pas
    //  " docked " : onLayout lui donne toute la page.
    {
        auto pal = std::make_unique<CommandPalette>("help.macros.palette");
        palette_ = pal.get();
        links_ += palette_->chosen->connect([this](const std::string& t) { onLink(t); });
        addChild(std::move(pal));
    }
    refreshDirty();
}

void LibraryHelpPage::onLayout() {
    ui::DockLayout::onLayout();
    if (palette_ != nullptr) palette_->setBounds(bounds());
}

// ---------------------------------------------------------------------------
void LibraryHelpPage::reload() {
    auto all = project::scanLibrary(libsRoot_);

    // LOT MACROS 1 : UN ONGLET PAR SORTE. L'onglet Macros ne montre que les
    // macros, rangees dans les dossiers de l'onglet Macros ; l'onglet Blocs,
    // tout le reste. Ce que l'autre onglet montre est retenu : un lien qui y
    // mene change d'onglet au lieu de dire " introuvable ".
    specs_.clear();
    elsewhere_.clear();
    std::vector<project::CatalogEntry> entries;
    entries.reserve(all.size());
    for (auto& e : all) {
        const bool macro = e.kind == project::CatalogKind::Macro;
        if (scope_ != HelpScope::All && (scope_ == HelpScope::Macros) != macro) {
            elsewhere_[e.name] = macro;
            continue;
        }
        entries.push_back(std::move(e));
    }
    TreeOptions options;
    options.rootLabel = rootLabel();
    if (scope_ == HelpScope::Macros) {
        namespace mm = project::macro;
        std::vector<std::pair<std::string, std::string>> known;
        for (const auto& e : entries) {
            std::ifstream in(e.path, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            auto spec = mm::parseMacroSpec(text.str(), e.name);
            known.emplace_back(e.name, spec.category);
            specs_[e.name] = std::move(spec);
        }
        // Le meme rangement que l'onglet Macros, lu du meme fichier.
        mm::MacroFolders folders((std::filesystem::path(libsRoot_) / "Macros" / "dossiers.txt").string());
        (void)folders.load();
        folders.setMacros(known);
        const auto layout = folders.arrange();
        std::vector<project::CatalogEntry> ordered;
        std::vector<bool> taken(entries.size(), false);
        for (const auto& m : layout.macros) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (taken[i] || entries[i].name != m.name) continue;
                taken[i] = true;
                ordered.push_back(std::move(entries[i]));
                options.folders.push_back(m.folder);
                break;
            }
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (taken[i]) continue;
            ordered.push_back(std::move(entries[i]));
            options.folders.push_back({});
        }
        entries = std::move(ordered);
        options.folderOrder = layout.folders;
    }
    const auto n = entries.size();

    treeModel_ = std::make_shared<LibraryHelpTreeModel>(std::move(entries), std::move(options));
    tree_->setModel(treeModel_);
    tree_->expandToDepth(1);

    fieldModel_ = std::make_shared<HelpFieldListModel>();
    fieldList_->setModel(fieldModel_);

    currentNode_ = ui::kInvalidNode;
    dirty_       = false;
    draft_       = {};
    fields_.clear();
    session_.reset();
    transient_ = false;

    if (n == 0) {
        // Un dossier vide n'est pas une erreur, c'est une information : on dit
        // OU on a cherche, parce que la cause est presque toujours le chemin.
        setStatus("aucune biblioth\xC3\xA8que trouv\xC3\xA9" "e dans " + libsRoot_, true);
        article_->setArticle({});
        source_->setText({});
        preview_->setText({});
        refreshCommands();
        return;
    }

    // On NE fait PAS ensureVisible ici : a ce moment l'arbre n'a pas encore
    // ete dispose, sa hauteur vaut zero, et le calcul de defilement part sur
    // une fenetre vide. Resultat observe : la page s'ouvrait avec l'item
    // selectionne deja sorti par le haut. Le premier item est en tete de toute
    // facon ; c'est le changement de selection ulterieur qui a besoin d'etre
    // ramene dans la vue.
    selectNode(treeModel_->nodeForEntry(0));

    std::size_t aDocumenter = 0;
    for (const auto& e : treeModel_->entries())
        if (!e.hasHelp || e.help.summary.empty()) ++aDocumenter;

    const std::string quoi = scope_ == HelpScope::Macros ? " macros dans "
                           : scope_ == HelpScope::Blocks ? " blocs et types dans " : " \xC3\xA9l\xC3\xA9ments dans ";
    std::string message =
        std::to_string(n) + quoi + libsRoot_
        + (aDocumenter ? "   \xC2\xB7   " + std::to_string(aDocumenter) + " sans r\xC3\xA9sum\xC3\xA9"
                       : "   \xC2\xB7   tous ont un r\xC3\xA9sum\xC3\xA9");

    // LES FAVORIS QUI NE DESIGNENT PLUS RIEN SE DISENT. Un bloc renomme ou un
    // parametre supprime laisse un favori qui n'ouvre rien ; l'effacer en
    // silence ferait croire a un bug de l'application, et le garder muet ferait
    // croire a un favori qui marche.
    if (const auto perdus = nav_.stale(treeModel_->entries()); !perdus.empty()) {
        message += "   \xC2\xB7   " + std::to_string(perdus.size())
                 + " favori(s) ne d\xC3\xA9signent plus rien : " + help::labelOf(perdus.front());
        if (perdus.size() > 1) message += ", ...";
    }
    setStatus(std::move(message));
    refreshCommands();
}

const project::CatalogEntry* LibraryHelpPage::current() const {
    return treeModel_ ? treeModel_->entry(currentNode_) : nullptr;
}

std::size_t LibraryHelpPage::entryCount() const {
    return treeModel_ ? treeModel_->entryCount() : 0;
}

std::size_t LibraryHelpPage::indexOfEntry(std::string_view name) const {
    if (!treeModel_) return static_cast<std::size_t>(-1);
    const auto& entries = treeModel_->entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) return i;
    return static_cast<std::size_t>(-1);
}

// ---------------------------------------------------------------------------
void LibraryHelpPage::selectNode(ui::NodeId n) {
    if (!treeModel_) return;
    const auto* e = treeModel_->entry(n);
    if (!e) return;                       // un dossier : on garde l'article ouvert

    if (dirty_ && n != currentNode_) {
        // On ne perd pas silencieusement ce qui est en cours. Le brouillon
        // reste attache a l'item qu'on quitte ; le dire est le minimum, et
        // c'est moins agacant qu'une boite modale a chaque clic dans l'arbre.
        setStatus("modifications non enregistr\xC3\xA9" "es sur "
                  + std::string(current() ? current()->name : std::string{})
                  + " : elles sont perdues en changeant d'\xC3\xA9l\xC3\xA9ment", true);
    }

    currentNode_ = n;
    draft_       = e->help;
    dirty_       = false;
    fields_      = helpFields(*e);

    fieldModel_->setFields(fields_, &draft_);
    currentField_ = 0;
    selectField(0);

    // Changer d'item jette l'essai en cours : le projet de poche appartenait a
    // l'exemple du bloc qu'on vient de quitter, et le garder vivant afficherait
    // les valeurs d'un bloc a cote de l'aide d'un autre - une table qui tourne
    // sous le nom d'un bloc qui n'a rien a voir est pire qu'une table vide.
    session_.reset();
    if (benchPane_ != nullptr) benchPane_->setBench(nullptr, nullptr);
    if (tabs_ != nullptr) tabs_->setTabLive(benchTab_, false);

    showEntry(*e);
    source_->setText(sourceOf(*e));
    refreshPreview();
    refreshTabTitles();
    refreshCommands();
}

void LibraryHelpPage::selectField(ui::RowIndex row) {
    if (row >= fields_.size()) return;
    currentField_ = row;
    const auto& f = fields_[row];

    editorBox_->setTitle(f.hint.empty() ? f.label : f.label + "   \xC2\xB7   " + f.hint);

    loading_ = true;
    editor_->setText(readField(draft_, f));
    editor_->clearModified();
    loading_ = false;
}

void LibraryHelpPage::applyFilter(const std::string& term) {
    if (!tree_ || !treeModel_) return;
    if (term.empty()) {
        tree_->setFilter({});
    } else {
        auto model = treeModel_;     // copie du shared_ptr : le predicat peut
                                     // survivre a un rechargement du modele
        tree_->setFilter([model, term](ui::NodeId n) { return model->matches(n, term); });
    }
    tree_->expandToDepth(2);
}

void LibraryHelpPage::onDraftEdited(const std::string& text) {
    if (loading_ || currentField_ >= fields_.size()) return;
    writeField(draft_, fields_[currentField_], text);
    dirty_ = true;
    // La liste des champs porte le marqueur "rempli / vide" : il doit bouger
    // pendant qu'on tape, sinon il decrit l'etat d'avant et ment.
    fieldList_->invalidate();
    refreshPreview();
    refreshTabTitles();
    setStatus("modifi\xC3\xA9, pas encore enregistr\xC3\xA9");
}

void LibraryHelpPage::refreshPreview() {
    const auto bloc = project::renderHelpBlock(draft_);
    preview_->setText(bloc.empty()
        ? std::string("(aucune aide : le fichier ne recevra aucune ligne #!)")
        : bloc);
}

void LibraryHelpPage::refreshTabTitles() {
    if (tabs_) tabs_->setTabModified(1, dirty_);
    refreshDirty();
}

void LibraryHelpPage::refreshDirty() {
    // RIEN A ENREGISTRER, RIEN A MONTRER. Deux boutons toujours presents se
    // lisent comme du decor ; qu'ils apparaissent quand on tape est la
    // confirmation que la frappe a ete prise.
    const auto vis = dirty_ ? ui::Visibility::Visible : ui::Visibility::Collapsed;
    if (save_ && save_->visibility() != vis)     save_->setVisibility(vis);
    if (revert_ && revert_->visibility() != vis) revert_->setVisibility(vis);
    if (toolbar_) toolbar_->invalidateLayout();
}

bool LibraryHelpPage::saveVisibleForTest() const noexcept {
    return save_ != nullptr && save_->visibility() == ui::Visibility::Visible;
}

std::size_t LibraryHelpPage::currentTabForTest() const noexcept {
    return tabs_ != nullptr ? tabs_->currentIndex() : 0;
}

bool LibraryHelpPage::save() {
    auto* e = treeModel_ ? treeModel_->mutableEntry(currentNode_) : nullptr;
    if (!e) { setStatus("rien \xC3\xA0 enregistrer : aucun \xC3\xA9l\xC3\xA9ment s\xC3\xA9lectionn\xC3\xA9", true); return false; }

    const auto r = project::saveHelp(*e, draft_);
    if (!r.ok) { setStatus("\xC3\xA9" "chec de l'enregistrement : " + r.message, true); return false; }

    // L'entree a ete relue depuis le disque : on repart de ce qu'elle contient
    // vraiment, pas de ce qu'on croyait y avoir mis.
    dirty_  = false;
    draft_  = e->help;
    fields_ = helpFields(*e);
    fieldModel_->setFields(fields_, &draft_);
    if (currentField_ >= fields_.size()) currentField_ = 0;
    selectField(currentField_);

    showEntry(*e);
    source_->setText(sourceOf(*e));
    refreshPreview();
    refreshTabTitles();
    tree_->invalidate();
    setStatus(e->name + " : " + r.message + "   \xC2\xB7   " + e->path);
    if (status_) status_->setTransientMessage(e->name + " enregistr\xC3\xA9 dans " + e->path, 6.0,
                                              ui::StatusBar::Severity::Success);
    return true;
}

void LibraryHelpPage::revert() {
    const auto* e = current();
    if (!e) return;
    draft_  = e->help;
    dirty_  = false;
    fields_ = helpFields(*e);
    fieldModel_->setFields(fields_, &draft_);
    if (currentField_ >= fields_.size()) currentField_ = 0;
    selectField(currentField_);
    refreshPreview();
    refreshTabTitles();
    setStatus("modifications abandonn\xC3\xA9" "es, le fichier n'a pas \xC3\xA9t\xC3\xA9 touch\xC3\xA9");
}

void LibraryHelpPage::setStatus(std::string message, bool error) {
    lastStatus_ = message;
    if (!status_) return;
    // Une erreur a son pictogramme et sa couleur ; le reste est une
    // information, attenuee, qui ne crie pas.
    status_->setMessage(std::move(message),
                        error ? ui::StatusBar::Severity::Error : ui::StatusBar::Severity::Info);
}

// =============================================================================
//  NAVIGUER
// =============================================================================
help::Target LibraryHelpPage::targetForNode(ui::NodeId n) const {
    help::Target t;
    if (!treeModel_) return t;
    if (const auto* e = treeModel_->entry(n); e != nullptr) {
        t.kind  = help::TargetKind::LibraryEntry;
        t.entry = e->name;
    }
    return t;                 // un dossier : rien a ouvrir
}

void LibraryHelpPage::goTo(const help::Target& target) {
    if (target.kind == help::TargetKind::None) return;
    nav_.go(target);
    showTarget(target);
    refreshCommands();
}

void LibraryHelpPage::showTarget(const help::Target& target) {
    switch (target.kind) {
        case help::TargetKind::None:
        case help::TargetKind::Topic:   // 1.11 : un sujet du centre d'aide, que le centre ouvre
            return;

        case help::TargetKind::LibraryEntry:
        case help::TargetKind::Parameter: {
            const std::size_t i = indexOfEntry(target.entry);
            if (i == static_cast<std::size_t>(-1)) {
                // Lot macros 1 : dans l'autre onglet (une macro vue des blocs,
                // un bloc vu des macros) : l'hote y va.
                if (const auto it = elsewhere_.find(target.entry); it != elsewhere_.end()) {
                    if (outOfScope_) { outOfScope_(target); return; }
                    setStatus(target.entry + (it->second ? " est une macro : onglet Macros de l'aide"
                                                         : " est un bloc : onglet Blocs DFB / DDT de l'aide"), true);
                    return;
                }
                setStatus(target.entry + " n'est plus dans la biblioth\xC3\xA8que", true);
                return;
            }
            const auto node = treeModel_->nodeForEntry(i);
            // L'ARBRE NE SUIT PAS TOUT SEUL. TreeView n'expose pas de selection
            // par programme (que currentNode() et ensureVisible) : on amene donc
            // la ligne dans la vue, et le curseur de l'arbre reste ou il est.
            // Une methode TreeView::select(NodeId) le reglerait en trois lignes
            // - c'est la seule chose qui manque a cette page.
            if (node != currentNode_) selectNode(node);
            else                      showEntry(treeModel_->entries()[i], target.subject);
            tree_->ensureVisible(node);

            if (!target.subject.empty()) {
                // Le parametre : on ouvre son champ dans l'onglet Modifier, qui
                // est le seul endroit ou un parametre a une ligne a lui.
                for (std::size_t f = 0; f < fields_.size(); ++f) {
                    if (fields_[f].key == target.subject) {
                        selectField(static_cast<ui::RowIndex>(f));
                        break;
                    }
                }
                setStatus(target.entry + " \xC2\xB7 " + target.subject);
            }
            return;
        }

        case help::TargetKind::Diagnostic: {
            if (const help::Code* c = help::find(target.entry); c != nullptr) {
                showArticle(help::article(*c), true);
                setStatus(target.entry + "   \xC2\xB7   " + std::string(c->title));
            }
            return;
        }

        case help::TargetKind::Glossary: {
            const help::Term* t = help::glossaryTerm(target.entry);
            if (t == nullptr) return;
            ui::HelpArticle a;
            ui::HelpBlock hero;
            hero.kind  = ui::HelpBlockKind::Hero;
            hero.text  = std::string(t->word);
            hero.label = "Un mot du m\xC3\xA9tier, tel que l'automate l'entend";
            hero.tone  = ui::Tone::Muted;
            hero.pills.push_back({"Glossaire", ui::kNoColor, ui::Tone::Info});
            a.blocks.push_back(std::move(hero));
            addBlock(a, ui::HelpBlockKind::Lead, std::string(t->short_));
            if (!t->long_.empty())
                addBlock(a, ui::HelpBlockKind::Paragraph, std::string(t->long_));
            showArticle(std::move(a), true);
            return;
        }
    }
}

void LibraryHelpPage::showEntry(const project::CatalogEntry& e, const std::string& subject) {
    // Lot macros 1 : le fil d'Ariane de l'onglet, et pour une macro son
    // formulaire (ce qu'elle lit, produit, enchaine, demande).
    ArticleContext context;
    context.rootLabel = rootLabel();
    if (treeModel_ && treeModel_->byFolder()) {
        context.byFolder = true;
        const auto i = indexOfEntry(e.name);
        if (i != static_cast<std::size_t>(-1)) context.folder = treeModel_->folderOfEntry(i);
    }
    context.spec      = macroSpec(e.name);
    context.canLaunch = static_cast<bool>(launchMacro_);
    auto a = buildHelpArticle(e, context);

    // LE BANDEAU DE VERSION. Il ne se calcule qu'ici parce que lui seul sait ce
    // que le PROJET a importe ; buildHelpArticle ne voit que le fichier. Le
    // bloc est pose juste apres le sous-titre, donc avant le resume : un
    // avertissement qu'on lit apres avoir lu la page arrive trop tard.
    if (projectVersion_) {
        const std::string notice =
            help::versionNotice(e.name, e.version, projectVersion_(e.name));
        if (!notice.empty()) {
            ui::HelpBlock note;
            note.kind = ui::HelpBlockKind::Note;
            note.text = notice;
            // Juste sous l'en-tete : c'est la premiere chose qu'on lit apres le
            // nom, et un ecart de version change le sens de tout le reste.
            const auto at = std::find_if(
                a.blocks.begin(), a.blocks.end(), [](const ui::HelpBlock& b) {
                    return b.kind == ui::HelpBlockKind::Hero
                        || b.kind == ui::HelpBlockKind::Subtitle;
                });
            a.blocks.insert(at == a.blocks.end() ? a.blocks.begin() : at + 1,
                            std::move(note));
        }
    }

    // " Utilise par ", calcule. `#! see` est ecrit a la main dans un fichier ;
    // les dix blocs qui prennent ce DDT en parametre ne sont ecrits nulle part,
    // et c'est pourtant la reponse a " qu'est-ce que je casse si je le change ".
    if (treeModel_) {
        const auto back = help::backlinks(treeModel_->entries(), e.name);
        if (!back.empty()) {
            // Des puces qu'on clique, pas une liste qu'on recopie dans la
            // recherche : la question " qu'est-ce que je casse " appelle
            // aussitot " montre-moi ".
            ui::HelpBlock links;
            links.kind  = ui::HelpBlockKind::Links;
            links.label = "Utilis\xC3\xA9 par";
            const auto& all = treeModel_->entries();
            for (const auto& b : back) {
                const auto it = std::find_if(all.begin(), all.end(),
                    [&](const project::CatalogEntry& x) { return x.name == b.name; });
                const int fam = it == all.end() ? 5 : familyIndex(it->category);
                links.links.push_back({b.name, "lib:" + b.name, b.name + " : " + b.why,
                                       ui::kNoColor, ui::familyTone(fam)});
            }
            // Avant le filet de fin (depuis / auteur) s'il y en a un.
            const auto sep = std::find_if(a.blocks.begin(), a.blocks.end(),
                [](const ui::HelpBlock& b) { return b.kind == ui::HelpBlockKind::Separator; });
            if (sep == a.blocks.end()) {
                ui::HelpBlock h;
                h.kind = ui::HelpBlockKind::Heading;
                h.text = "Utilis\xC3\xA9 par";
                a.blocks.push_back(std::move(h));
                links.label.clear();
                a.blocks.push_back(std::move(links));
            } else {
                a.blocks.insert(sep, std::move(links));
            }
        }
    }

    // LES TYPES DEVIENNENT DES LIENS. " ARRAY[0..15] OF ST_SEQ_Member " sur la
    // carte d'un parametre designe un DDT de la bibliotheque : son survol dit
    // ce que c'est, son clic y mene. Un mot du glossaire (EBOOL, TIME) dit ce
    // qu'il veut dire. Calcule ici parce que seule la page connait toute la
    // bibliotheque ; buildHelpArticle ne voit que le fichier.
    if (treeModel_) {
        const auto& all = treeModel_->entries();
        for (auto& b : a.blocks) {
            if (b.kind != ui::HelpBlockKind::Term) continue;
            for (auto& p : b.pills) {
                if (!p.target.empty() || p.tone != ui::Tone::None) continue;
                for (const auto& x : all) {
                    if (x.name == e.name || p.text.find(x.name) == std::string::npos) continue;
                    const auto at = p.text.find(x.name);
                    const auto end = at + x.name.size();
                    const bool whole = (at == 0 || !(std::isalnum(static_cast<unsigned char>(p.text[at - 1])) || p.text[at - 1] == '_'))
                        && (end >= p.text.size() || !(std::isalnum(static_cast<unsigned char>(p.text[end])) || p.text[end] == '_'));
                    if (!whole) continue;
                    p.target = "lib:" + x.name;
                    p.tip = x.name + " : " + (x.help.summary.empty() ? std::string("pas encore de r\xC3\xA9sum\xC3\xA9")
                                                                    : reflow(x.help.summary));
                    p.tone = ui::familyTone(familyIndex(x.category));
                    break;
                }
                if (p.target.empty()) {
                    if (const auto* g = help::glossaryTerm(p.text); g != nullptr) {
                        p.target = "glo:" + std::string(g->word);
                        p.tip = std::string(g->word) + " : " + std::string(g->short_);
                    }
                }
            }
        }
    }

    (void)subject;
    showArticle(std::move(a), false);
}

void LibraryHelpPage::showArticle(ui::HelpArticle a, bool transient) {
    transient_ = transient;
    article_->setArticle(std::move(a));
    article_->scrollToTop();
    if (tabs_) tabs_->setCurrentIndex(0);   // l'article est dans le premier onglet
}

const ui::HelpArticle& LibraryHelpPage::articleForTest() const noexcept {
    return article_->article();
}

void LibraryHelpPage::restoreNavigation(const std::vector<std::string>& recents,
                                        const std::vector<std::string>& favourites) {
    nav_.restore(recents, favourites);
    refreshCommands();
}

void LibraryHelpPage::refreshPlaces() {
    if (!places_ || !placesModel_) return;

    std::vector<PlacesListModel::Row> rows;
    placeTargets_.clear();

    const auto pousser = [&](const std::string& titre, const std::vector<help::Target>& liste,
                             bool favoris) {
        if (liste.empty()) return;
        rows.push_back({titre, ui::Icon::None, ui::Tone::None, true, false});
        placeTargets_.push_back({});                 // un titre n'ouvre rien
        for (const auto& t : liste) {
            PlacesListModel::Row r{help::labelOf(t), ui::Icon::Document, ui::Tone::None, false, false};
            if (favoris) {
                r.icon = ui::Icon::StarFilled;
                r.tone = ui::Tone::Warning;
            } else if (t.kind == help::TargetKind::Diagnostic) {
                r.icon = ui::Icon::Info;
                r.tone = ui::Tone::Info;
            } else if (const auto i = indexOfEntry(t.entry); i != static_cast<std::size_t>(-1)) {
                const auto& e = treeModel_->entries()[i];
                r.icon = t.kind == help::TargetKind::Parameter ? ui::Icon::Variable : kindIcon(e.kind);
                r.tone = ui::familyTone(familyIndex(e.category));
            }
            rows.push_back(std::move(r));
            placeTargets_.push_back(t);
        }
    };

    pousser("Gard\xC3\xA9" "es", nav_.favourites(), true);
    pousser("R\xC3\xA9" "centes", nav_.recents(), false);

    if (rows.empty()) {
        rows.push_back({"Rien de gard\xC3\xA9 pour l'instant : l'\xC3\xA9toile de la barre garde une page",
                        ui::Icon::Star, ui::Tone::None, false, true});
        placeTargets_.push_back({});
    }

    std::static_pointer_cast<PlacesListModel>(placesModel_)->setRows(std::move(rows));
    // setModel et pas invalidate : la selection de la liste designait une LIGNE,
    // et la ligne vient de changer de contenu - la garder surlignerait une
    // autre page que celle qu'on a ouverte.
    places_->setModel(placesModel_);
}

// =============================================================================
//  LES LIENS, LES DOSSIERS, LA PALETTE
// =============================================================================
void LibraryHelpPage::onLink(const std::string& target) {
    if (target.rfind("copy:", 0) == 0) {
        // Le widget a deja copie ; on le dit dans le bandeau aussi.
        if (const auto* e = current(); e != nullptr)
            setStatus("exemple de " + e->name + " copi\xC3\xA9 dans le presse-papier");
        return;
    }
    if (target.rfind("cmd:", 0) == 0) {
        runCommand(std::atoi(target.c_str() + 4));
        return;
    }
    if (target.rfind("cat:", 0) == 0) {
        showCategory(target.substr(4));
        return;
    }
    if (target.rfind("run:", 0) == 0) {
        // Lot macros 1 : " Lancer " - l'hote ouvre le formulaire.
        if (launchMacro_) launchMacro_(target.substr(4));
        return;
    }
    if (target == "diag:index") {
        showDiagnostics();
        return;
    }
    if (target == "palette:") {
        openPalette();
        return;
    }
    const help::Target t = help::parseTarget(target);
    if (t.kind == help::TargetKind::None) return;
    goTo(t);
    // Un parametre sans aide, clique dans l'anneau de couverture : on vient
    // l'ECRIRE. Le champ est deja choisi par showTarget ; on ouvre l'onglet.
    if (t.kind == help::TargetKind::Parameter && tabs_ != nullptr) {
        tabs_->setCurrentIndex(1);
        setStatus("\xC3\xA9" "crivez l'aide de " + t.subject + ", puis Enregistrer");
    }
}

void LibraryHelpPage::showCategory(const std::string& category) {
    if (!treeModel_) return;
    if (treeModel_->byFolder()) {
        // Lot macros 1 : un dossier de l'onglet Macros, et ses sous-dossiers.
        showArticle(buildFolderOverview(treeModel_->entries(), treeModel_->entryFolders(), category,
                                        rootLabel()), true);
        setStatus(category.empty() ? "toutes les macros ; chaque puce ouvre sa page"
                                   : project::macro::folderLeaf(category) + " ; chaque puce ouvre sa page");
        return;
    }
    showArticle(buildLibraryOverview(treeModel_->entries(), category, rootLabel()), true);
    setStatus(category.empty() ? (scope_ == HelpScope::Blocks ? std::string("tous les blocs et les types ; chaque puce ouvre sa page")
                                                              : std::string("toute la biblioth\xC3\xA8que ; chaque puce ouvre sa page"))
                               : categoryLabel(category) + " ; chaque puce ouvre sa page");
}

void LibraryHelpPage::openPalette() {
    if (palette_ == nullptr || !treeModel_) return;
    std::vector<PaletteItem> items;

    // Les commandes d'abord : ce qu'on peut FAIRE ici, avec leur raison quand
    // on ne peut pas.
    for (const auto& c : commands()) {
        if (c.separator) continue;
        PaletteItem it;
        it.title    = c.label;
        it.detail   = c.enabled ? "Commande" : "Indisponible : " + c.reason;
        it.target   = "cmd:" + std::to_string(c.id);
        it.icon     = c.icon;
        it.tone     = ui::Tone::Accent;
        it.shortcut = c.shortcut;
        it.enabled  = c.enabled;
        items.push_back(std::move(it));
    }
    const std::string racine = rootLabel();
    items.push_back({racine.empty() ? std::string("Toute la biblioth\xC3\xA8que") : "Tout l'onglet " + racine,
                     "Vue d'ensemble et couverture", "cat:",
                     ui::Icon::Library, ui::Tone::Accent, {}, "accueil bibliotheques", true});

    // Les dossiers, puis chaque element. Lot macros 1 : dans l'onglet Macros,
    // les dossiers de l'onglet Macros (chemins complets).
    const bool parDossier = treeModel_->byFolder();
    const auto& dossiers = treeModel_->entryFolders();
    std::vector<std::string> cats;
    for (std::size_t i = 0; i < treeModel_->entries().size(); ++i) {
        const auto c = parDossier ? dossiers[i] : treeModel_->entries()[i].category;
        if (parDossier && c.empty()) continue;
        if (std::find(cats.begin(), cats.end(), c) == cats.end()) cats.push_back(c);
    }
    for (const auto& c : cats) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < treeModel_->entries().size(); ++i)
            n += (parDossier ? dossiers[i] : treeModel_->entries()[i].category) == c;
        std::string titre = parDossier ? c : categoryLabel(c);
        if (parDossier)
            for (std::size_t at = titre.find('/'); at != std::string::npos; at = titre.find('/', at + 3))
                titre.replace(at, 1, " \xC2\xBB ");
        items.push_back({titre, "Dossier   \xC2\xB7   " + std::to_string(n) + (parDossier ? " macros" : " \xC3\xA9l\xC3\xA9ments"),
                         "cat:" + c, ui::Icon::Folder,
                         ui::familyTone(parDossier ? 4 : familyIndex(c)), {}, c, true});
    }
    for (std::size_t i = 0; i < treeModel_->entries().size(); ++i) {
        const auto& e = treeModel_->entries()[i];
        PaletteItem it;
        it.title  = e.name;
        it.detail = std::string(kindName(e.kind)) + "   \xC2\xB7   "
                  + (parDossier ? (dossiers[i].empty() ? racine : dossiers[i]) : categoryLabel(e.category));
        if (!e.help.summary.empty()) {
            std::string resume = reflow(e.help.summary);
            if (resume.size() > 90) {
                // La coupe recule au debut d'un caractere : un resume accentue
                // coupe au milieu d'un " e " laisserait un octet orphelin.
                std::size_t cut = 87;
                while (cut > 0 && (static_cast<unsigned char>(resume[cut]) & 0xC0) == 0x80) --cut;
                resume = resume.substr(0, cut) + "...";
            }
            it.detail += "   \xC2\xB7   " + resume;
        }
        it.target = "lib:" + e.name;
        it.icon   = kindIcon(e.kind);
        it.tone   = ui::familyTone(familyIndex(e.category));
        // Les parametres sont cherches aussi : taper " Thermal " trouve les
        // blocs qui en ont un.
        for (const auto& d : e.declarations)
            if (!d.isLocal()) it.keywords += d.name + " ";
        items.push_back(std::move(it));
    }
    // Les codes de diagnostic et le glossaire, en dernier.
    for (const auto& code : help::allCodes()) {
        items.push_back({std::string(code.id), std::string(code.title),
                         "dia:" + std::string(code.id),
                         code.severity == help::Severity::Error ? ui::Icon::Error
                         : code.severity == help::Severity::Warning ? ui::Icon::Warning : ui::Icon::Info,
                         code.severity == help::Severity::Error ? ui::Tone::Error
                         : code.severity == help::Severity::Warning ? ui::Tone::Warning : ui::Tone::Info,
                         {}, "diagnostic", true});
    }
    for (const auto& term : help::glossary())
        items.push_back({std::string(term.word), "Glossaire   \xC2\xB7   " + std::string(term.short_),
                         "glo:" + std::string(term.word), ui::Icon::Document, ui::Tone::Muted,
                         {}, "glossaire", true});

    palette_->open(std::move(items));
    palette_->setBounds(bounds());
}

// =============================================================================
//  CHERCHER
// =============================================================================
void LibraryHelpPage::runSearch(const std::string& term) {
    if (!treeModel_) return;

    // UN ESSAI EN COURS DETOURNE LA BARRE. " Fbk = 1 " force une entree et
    // relance : c'est le geste qui fait comprendre un bloc, et il n'avait pas
    // de place a lui. Quand il n'y a pas d'essai, c'est une recherche comme
    // les autres.
    if (benchPane_ != nullptr && static_cast<BenchPane*>(benchPane_)->bench() != nullptr) {
        const auto eq = term.find('=');
        if (eq != std::string::npos) {
            const auto cut = [](std::string s) {
                while (!s.empty() && static_cast<unsigned char>(s.front()) <= ' ')
                    s.erase(s.begin());
                while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ')
                    s.pop_back();
                return s;
            };
            const std::string nom    = cut(term.substr(0, eq));
            const std::string valeur = cut(term.substr(eq + 1));
            if (forceForTest(nom, valeur)) return;
            setStatus(nom + " n'est pas une variable de cet exemple", true);
            return;
        }
    }

    lastSearch_ = term;
    if (term.empty()) { setStatus("tapez un terme, puis Entr\xC3\xA9" "e"); return; }

    const auto hits = help::search(treeModel_->entries(), term);
    if (hits.empty()) {
        setStatus("rien pour \xC2\xAB " + term + " \xC2\xBB", true);
        // UN ETAT VIDE, PAS UNE PAGE BLANCHE : un pictogramme, une phrase, et
        // les deux gestes qui peuvent aider.
        ui::HelpArticle a;
        ui::HelpBlock vide;
        vide.kind = ui::HelpBlockKind::Empty;
        vide.icon = ui::Icon::Search;
        vide.text = "Aucun r\xC3\xA9sultat pour \xC2\xAB " + term + " \xC2\xBB : ni dans un nom, ni dans un "
                    "param\xC3\xA8tre, ni dans un code de d\xC3\xA9" "faut, ni dans le glossaire.";
        vide.links.push_back({"Parcourir la biblioth\xC3\xA8que", "cat:", {}, ui::kNoColor, ui::Tone::Accent});
        vide.links.push_back({"Les codes de diagnostic", "cmd:" + std::to_string(CmdDiagnostics), {},
                              ui::kNoColor, ui::Tone::None});
        a.blocks.push_back(std::move(vide));
        showArticle(std::move(a), true);
        return;
    }

    ui::HelpArticle a;
    addBlock(a, ui::HelpBlockKind::Title,
             std::to_string(hits.size()) + (hits.size() > 1 ? " r\xC3\xA9sultats" : " r\xC3\xA9sultat"));
    addBlock(a, ui::HelpBlockKind::Subtitle, "pour \xC2\xAB " + term + " \xC2\xBB");
    addBlock(a, ui::HelpBlockKind::Paragraph,
             "Les r\xC3\xA9sultats sont class\xC3\xA9s : un nom exact passe devant un param\xC3\xA8tre, un "
             "param\xC3\xA8tre devant une phrase qui contient le mot.");

    for (const auto& h : hits) {
        // UNE CARTE PAR RESULTAT, ET ELLE SE CLIQUE. La liste etait du texte :
        // on lisait le nom, puis on allait le chercher dans l'arbre.
        ui::HelpBlock b;
        b.kind   = ui::HelpBlockKind::Term;
        b.label  = h.subject.empty() ? h.entry : h.entry + " \xC2\xB7 " + h.subject;
        b.detail = std::string(kindWord(h.kind));
        b.text   = h.excerpt;
        b.pills.push_back({std::string(kindWord(h.kind)), ui::kNoColor, ui::Tone::Accent});
        help::Target cible{help::TargetKind::LibraryEntry, h.entry, {}};
        if (h.kind == help::HitKind::Diagnostic) cible = {help::TargetKind::Diagnostic, h.entry, {}};
        else if (h.kind == help::HitKind::Glossary) cible = {help::TargetKind::Glossary, h.entry, {}};
        else if (h.kind == help::HitKind::Param) cible = {help::TargetKind::Parameter, h.entry, h.subject};
        if (const auto i = indexOfEntry(h.entry); i != static_cast<std::size_t>(-1)) {
            const auto& e = treeModel_->entries()[i];
            b.pills.push_back({categoryLabel(e.category), ui::kNoColor,
                               ui::familyTone(familyIndex(e.category))});
        }
        b.links.push_back({b.label, help::formatTarget(cible), "Ouvrir " + b.label,
                           ui::kNoColor, ui::Tone::None});
        a.blocks.push_back(std::move(b));
    }
    showArticle(std::move(a), true);

    // Ouvrir le premier : une recherche qui rend une liste et laisse l'utilisateur
    // la parcourir des yeux n'a fait que la moitie du travail. La liste reste
    // affichee jusqu'au prochain clic, donc on ne perd rien.
    const auto& first = hits.front();
    help::Target cible;
    switch (first.kind) {
        case help::HitKind::Diagnostic:
            cible = {help::TargetKind::Diagnostic, first.entry, {}};
            break;
        case help::HitKind::Glossary:
            cible = {help::TargetKind::Glossary, first.entry, {}};
            break;
        case help::HitKind::Param:
            cible = {help::TargetKind::Parameter, first.entry, first.subject};
            break;
        default:
            cible = {help::TargetKind::LibraryEntry, first.entry, {}};
            break;
    }
    nav_.go(cible);
    setStatus(std::to_string(hits.size()) + " r\xC3\xA9sultat(s) ; le meilleur est \xC2\xAB "
              + help::labelOf(cible) + " \xC2\xBB");
    applyFilter(term);
    refreshCommands();
}

void LibraryHelpPage::showDiagnostics() {
    showArticle(help::indexArticle(), true);
    setStatus("les codes de diagnostic ; chaque carte ouvre la page de son code");
}

// =============================================================================
//  ESSAYER
// =============================================================================
namespace {

} // namespace

void LibraryHelpPage::runExample() {
    const auto* e = current();
    if (e == nullptr || !help::hasExample(*e)) return;

    auto built = help::TrySession::build(*e, treeModel_->entries());
    if (!built) {
        // Le code rend le message cherchable : " XPG-3103 " est un terme exact,
        // " n'est pas declare " ne l'est pas.
        setStatus(help::withCode(built.error().message()), true);
        return;
    }
    session_ = *built;

    // LE BANC, DANS SON ONGLET. On y arrive a l'arret, avec la table deja
    // remplie : une table vide oblige a deviner qu'il faut appuyer sur quelque
    // chose, et c'est exactement ce qu'on reproche a l'ancienne version.
    if (benchPane_ != nullptr) {
        benchPane_->setBench(std::make_shared<help::TryBench>(session_), e);
        if (tabs_) tabs_->setCurrentIndex(benchTab_);
    }
    setStatus(e->name + " : l'exemple tourne dans l'onglet Essai. Marche pour encha\xC3\xAEner "
                        "les cycles, Un cycle pour avancer pas \xC3\xA0 pas, clic sur une LED pour "
                        "basculer une entr\xC3\xA9" "e");
    refreshCommands();
}

void LibraryHelpPage::stepExample() {
    if (benchPane_ == nullptr) return;
    if (benchPane_->bench() == nullptr) { runExample(); return; }
    // F5 sur un essai deja ouvert ramene a son onglet, et fait un cycle : c'est
    // le geste " encore un pas ".
    benchPane_->bench()->step();
    benchPane_->refresh();
    if (tabs_) tabs_->setCurrentIndex(benchTab_);
}

void LibraryHelpPage::tick(double deltaSeconds) {
    if (benchPane_ == nullptr || tabs_ == nullptr) return;
    // On ne fait tourner le banc QUE quand son onglet est devant. Un essai qui
    // continue derriere un autre onglet consomme, et surtout ment : on revient
    // quinze minutes plus tard sur un compteur a quarante mille sans avoir rien
    // demande.
    if (tabs_->currentIndex() != benchTab_) return;
    benchPane_->tick(deltaSeconds);
}

bool LibraryHelpPage::forceForTest(const std::string& name, const std::string& value) {
    if (benchPane_ == nullptr) return false;
    if (benchPane_->bench() == nullptr || !benchPane_->bench()->force(name, value)) return false;
    benchPane_->refresh();
    setStatus(name + " forc\xC3\xA9 \xC3\xA0 " + value + "   \xC2\xB7   le cycle a \xC3\xA9t\xC3\xA9 relanc\xC3\xA9 ; le cadenas "
              "de la table le lib\xC3\xA8re");
    return true;
}

// =============================================================================
//  LES COMMANDES
// =============================================================================
std::vector<HelpCommand> LibraryHelpPage::commands() const {
    const auto* e = current();
    const bool  exemple = e != nullptr && help::hasExample(*e);
    // La raison est jugee MAINTENANT, contre l'etat reel. Une entree grisee
    // sans raison est une entree dont on ne sait pas quoi faire.
    const std::string sansExemple =
        e == nullptr ? "aucun \xC3\xA9l\xC3\xA9ment s\xC3\xA9lectionn\xC3\xA9" : "ce bloc n'a pas d'exemple";

    std::string sansSection = "aucune section ouverte dans le projet";
    if (insertExample_) {
        const std::string nom = sectionName_ ? sectionName_() : std::string{};
        sansSection = nom.empty() ? "aucune section ouverte dans le projet" : "";
    }

    std::vector<HelpCommand> out;
    out.push_back({"Essayer l'exemple", "F5", exemple ? "" : sansExemple,
                   ui::Icon::Play, exemple, false, CmdTry});
    out.push_back({"Copier l'exemple", "", exemple ? "" : sansExemple,
                   ui::Icon::Document, exemple, false, CmdCopyExample});
    {
        const std::string nom = (insertExample_ && sectionName_) ? sectionName_() : std::string{};
        const std::string label =
            nom.empty() ? "Ins\xC3\xA9rer l'exemple dans la section"
                        : "Ins\xC3\xA9rer l'exemple dans " + nom;
        const std::string raison = !exemple ? sansExemple : sansSection;
        out.push_back({label, "", raison, ui::Icon::Section, raison.empty(), false,
                       CmdInsertExample});
    }
    out.push_back({"", "", "", ui::Icon::None, true, true, CmdNone});

    {
        const bool aFichier = e != nullptr && !e->path.empty();
        out.push_back({"Ouvrir le fichier source", "", aFichier
                           ? "" : "cette entr\xC3\xA9" "e n'a pas de fichier sur le disque",
                       ui::Icon::Open, aFichier, false, CmdOpenSource});
    }
    {
        const bool favori = nav_.isFavourite(nav_.current());
        out.push_back({favori ? "Retirer des favoris" : "Garder cette page",
                       "", nav_.current().kind == help::TargetKind::None
                           ? "aucune page ouverte" : "",
                       favori ? ui::Icon::StarFilled : ui::Icon::Star,
                       nav_.current().kind != help::TargetKind::None, false, CmdFavourite});
    }
    out.push_back({"", "", "", ui::Icon::None, true, true, CmdNone});

    out.push_back({"Pr\xC3\xA9" "c\xC3\xA9" "dent", "Alt+Gauche", nav_.canBack() ? "" : "rien derri\xC3\xA8re",
                   ui::Icon::Collapse, nav_.canBack(), false, CmdBack});
    out.push_back({"Suivant", "Alt+Droite", nav_.canForward() ? "" : "rien devant",
                   ui::Icon::Expand, nav_.canForward(), false, CmdForward});
    out.push_back({"", "", "", ui::Icon::None, true, true, CmdNone});

    out.push_back({"Codes de diagnostic", "F1", "", ui::Icon::Info, true, false,
                   CmdDiagnostics});
    out.push_back({"Imprimer cette page...", "Ctrl+P", "", ui::Icon::Print, true, false,
                   CmdPrintPage});
    out.push_back({"Exporter le dossier d'aide...", "",
                   entryCount() == 0 ? "la biblioth\xC3\xA8que est vide" : "",
                   ui::Icon::Export, entryCount() != 0, false, CmdExportDossier});
    out.push_back({"Revoir la visite guid\xC3\xA9" "e", "", "", ui::Icon::Info, true, false, CmdTour});
    return out;
}

void LibraryHelpPage::runCommand(int id) {
    switch (id) {
        case CmdBack:            showTarget(nav_.back());    refreshCommands(); break;
        case CmdForward:         showTarget(nav_.forward()); refreshCommands(); break;
        case CmdFavourite: {
            const bool pose = nav_.toggleFavourite(nav_.current());
            setStatus(pose ? "page gard\xC3\xA9" "e : " + help::labelOf(nav_.current())
                           : "page retir\xC3\xA9" "e des favoris");
            refreshCommands();
            break;
        }
        case CmdTry:             session_ ? stepExample() : runExample(); break;
        case CmdCopyExample:     copyExample();              break;
        case CmdInsertExample:   insertExampleIntoSection(); break;
        case CmdOpenSource:      openSourceFile();           break;
        case CmdPrintPage:       printCurrentPage();         break;
        case CmdExportDossier:   exportDossier();            break;
        case CmdDiagnostics:     showDiagnostics();          break;
        case CmdTour:            startTour();                break;
        default: break;
    }
}

void LibraryHelpPage::refreshCommands() {
    if (back_)    back_->setEnabled(nav_.canBack());
    if (forward_) forward_->setEnabled(nav_.canForward());
    if (star_) {
        // L'etoile se remplit, et se colore : " gardee " se voit sans se lire.
        const bool favori = nav_.isFavourite(nav_.current());
        star_->setText(favori ? "Gard\xC3\xA9" "e" : "Garder");
        star_->setIcon(favori ? ui::Icon::StarFilled : ui::Icon::Star);
        star_->setIconTone(favori ? ui::Tone::Warning : ui::Tone::None);
        star_->setEnabled(nav_.current().kind != help::TargetKind::None);
    }
    if (tryIt_) {
        const auto* e = current();
        tryIt_->setEnabled(e != nullptr && help::hasExample(*e));
    }
    refreshPlaces();
}

// =============================================================================
//  REPRENDRE L'EXEMPLE, OUVRIR LE FICHIER
// =============================================================================
void LibraryHelpPage::copyExample() {
    const auto* e = current();
    if (e == nullptr || !help::hasExample(*e)) return;
    ui::setClipboardText(help::copyText(*e));
    setStatus("exemple de " + e->name + " copi\xC3\xA9 dans le presse-papier");
}

void LibraryHelpPage::insertExampleIntoSection() {
    const auto* e = current();
    if (e == nullptr || !help::hasExample(*e)) return;
    if (!insertExample_) {
        // On ne fait pas semblant : la page ne connait pas le projet, et le
        // dire vaut mieux qu'un bouton qui ne fait rien.
        setStatus("cette page n'est pas reli\xC3\xA9" "e \xC3\xA0 un projet ouvert", true);
        return;
    }
    if (insertExample_(*e)) setStatus("exemple ins\xC3\xA9r\xC3\xA9   \xC2\xB7   Ctrl+Z pour annuler");
    else                    setStatus("l'insertion n'a pas eu lieu", true);
}

void LibraryHelpPage::openSourceFile() {
    const auto* e = current();
    if (e == nullptr) return;
    const std::string subject =
        currentField_ < fields_.size() ? fields_[currentField_].key : std::string{};
    const auto loc = help::locate(*e, subject);
    const std::string erreur = help::openInEditor(loc);
    if (!erreur.empty()) { setStatus(erreur, true); return; }
    setStatus(loc.path + " : ligne " + std::to_string(loc.line));
}

// =============================================================================
//  SORTIR DE L'ECRAN
// =============================================================================
std::string LibraryHelpPage::writeExport(const std::string& suggested,
                                         const std::string& html) {
    std::string chemin = chooseSavePath_ ? chooseSavePath_(suggested) : std::string{};
    if (chooseSavePath_ && chemin.empty()) return {};       // annule : rien a dire

    // PAS A COTE DE libs/. La premiere version ecrivait dans le dossier parent
    // de la bibliotheque - c'est-a-dire, sur une installation normale, dans
    // C:\Program Files, ou un utilisateur n'a pas le droit d'ecrire. Le bouton
    // semblait ne rien faire, et le message d'erreur passait dans un bandeau
    // que personne ne regarde a ce moment-la.
    //
    // On ecrit donc dans le dossier temporaire de la session, qui existe et est
    // inscriptible partout, et on OUVRE le fichier : quelque chose doit se
    // passer a l'ecran, sinon le bouton est casse du point de vue de celui qui
    // clique, quoi qu'en dise le bandeau.
    if (chemin.empty()) chemin = defaultExportDir() + "/" + suggested;
    return writeExportTo(std::move(chemin), html);
}

std::string LibraryHelpPage::writeExportTo(std::string chemin, const std::string& html) {
    const std::string erreur = help::writeHtmlFile(chemin, html);
    if (!erreur.empty()) { setStatus(erreur, true); return {}; }

    // L'ouverture n'est pas obligatoire : un export ecrit et non ouvert reste
    // un export reussi, donc son echec se dit sans annuler le reste.
    help::SourceLocation loc;
    loc.path  = chemin;
    loc.found = true;
    if (const auto ouvert = help::openInEditor(loc); !ouvert.empty())
        setStatus(chemin + "   \xC2\xB7   \xC3\xA9" "crit, mais " + ouvert, true);
    return chemin;
}

void LibraryHelpPage::exportDossier() {
    if (!treeModel_ || treeModel_->entryCount() == 0) return;

    help::DossierInfo info = dossier_;
    if (info.libsRoot.empty()) info.libsRoot = libsRoot_;

    const std::string html = help::renderDossierHtml(treeModel_->entries(), info);
    const std::size_t n    = treeModel_->entryCount();
    const auto dire = [this, n](const std::string& ou) {
        if (!ou.empty())
            setStatus("dossier \xC3\xA9" "crit : " + ou + "   \xC2\xB7   " + std::to_string(n)
                      + " \xC3\xA9l\xC3\xA9ments, il s'ouvre et s'imprime sans rien installer");
    };
    // OU L'ECRIRE : l'explorateur ("Enregistrer sous"), quand il y en a un et
    // qu'aucun autre choix n'a ete pose (setSavePathChooser). Il repond plus
    // tard : la page a pu etre fermee entretemps. Sans explorateur (essais,
    // console) : le dossier temporaire, comme avant.
    if (!chooseSavePath_ && ui::canPickFile()) {
        const std::weak_ptr<char> vivante = alive_;
        const auto propose = defaultExportDir() + "/" + help::suggestedFileName(info);
        const bool ouvert = ui::browsePath(ui::saveFile("Pages HTML|*.html;*.htm", propose, "Exporter le dossier d'aide"), {},
                                           [this, vivante, html, dire](std::string chemin) {
                                               if (vivante.expired()) return;
                                               dire(writeExportTo(std::move(chemin), html));
                                           });
        if (ouvert) return;
    }
    dire(writeExport(help::suggestedFileName(info), html));
}

void LibraryHelpPage::printCurrentPage() {
    const auto* e = current();
    const std::string titre = transient_ || e == nullptr ? "Aide" : e->name;
    // C'EST L'ARTICLE AFFICHE QUI PART, pas l'entree : ce qu'on imprime est ce
    // qu'on a sous les yeux, y compris un releve d'essai ou une page de
    // diagnostic. Une impression qui reconstruirait la page pourrait en sortir
    // une autre.
    const std::string html = help::renderArticleHtml(article_->article(), titre);
    const std::string ou   = writeExport("Aide-" + titre + ".html", html);
    if (!ou.empty())
        setStatus("page \xC3\xA9" "crite : " + ou + "   \xC2\xB7   ouvrez-la et imprimez depuis le navigateur");
}

void LibraryHelpPage::startTour() {
    ui::HelpArticle a;
    ui::HelpBlock hero;
    hero.kind  = ui::HelpBlockKind::Hero;
    hero.text  = "Cinq choses, et on vous laisse";
    hero.label = "Cette page ne s'ouvre qu'une fois ; \xC2\xAB Revoir la visite guid\xC3\xA9" "e \xC2\xBB la rappelle.";
    hero.tone  = ui::Tone::Accent;
    hero.pills.push_back({"Visite guid\xC3\xA9" "e", ui::kNoColor, ui::Tone::Accent});
    a.blocks.push_back(std::move(hero));

    int n = 0;
    for (const auto& step : help::tour()) {
        ui::HelpBlock b;
        b.kind   = ui::HelpBlockKind::Bullet;
        b.label  = std::to_string(++n);
        b.detail = std::string(step.title);
        b.text   = std::string(step.text);
        b.tone   = ui::Tone::Accent;
        a.blocks.push_back(std::move(b));
    }
    ui::HelpBlock fin;
    fin.kind = ui::HelpBlockKind::Links;
    fin.links.push_back({"Commencer par la biblioth\xC3\xA8que", "cat:", {}, ui::kNoColor, ui::Tone::Accent});
    fin.links.push_back({"Aller \xC3\xA0 un bloc (Ctrl+F)", "palette:", {}, ui::kNoColor, ui::Tone::None});
    a.blocks.push_back(std::move(fin));
    showArticle(std::move(a), true);
    setStatus("visite guid\xC3\xA9" "e   \xC2\xB7   \xC3\x89" "chap pour revenir \xC3\xA0 l'aide");
}

// =============================================================================
//  LE CLAVIER
// =============================================================================
ui::EventResult LibraryHelpPage::onEvent(const ui::InputEvent& ev) {
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (k == nullptr) return ui::DockLayout::onEvent(ev);

    // Alt+Gauche / Alt+Droite : l'historique, comme dans un navigateur.
    if (k->mods.alt && k->key == ui::Key::Left && nav_.canBack()) {
        runCommand(CmdBack);
        return ui::EventResult::Consumed;
    }
    if (k->mods.alt && k->key == ui::Key::Right && nav_.canForward()) {
        runCommand(CmdForward);
        return ui::EventResult::Consumed;
    }
    if (k->mods.ctrl && k->key == ui::Key::P) {
        runCommand(CmdPrintPage);
        return ui::EventResult::Consumed;
    }
    // Ctrl+F : la palette. Chercher partout et y aller, sans la souris.
    if (k->mods.ctrl && !k->mods.alt && k->key == ui::Key::F) {
        openPalette();
        return ui::EventResult::Consumed;
    }
    if (k->mods.none() && k->key == ui::Key::F5) {
        runCommand(CmdTry);
        return ui::EventResult::Consumed;
    }
    if (k->mods.none() && k->key == ui::Key::F1) {
        runCommand(CmdDiagnostics);
        return ui::EventResult::Consumed;
    }
    // Echap revient a l'aide de l'item selectionne. Sans ca, un resultat de
    // recherche ou un releve d'essai reste affiche jusqu'a ce qu'on reclique
    // dans l'arbre, ce qui donne l'impression que la page s'est bloquee.
    if (k->mods.none() && k->key == ui::Key::Escape && transient_) {
        if (const auto* e = current(); e != nullptr) {
            showEntry(*e);
            setStatus(e->name);
            return ui::EventResult::Consumed;
        }
    }
    return ui::DockLayout::onEvent(ev);
}

// --- seams de test ---------------------------------------------------------
void LibraryHelpPage::selectEntryForTest(std::size_t index) {
    // `goTo`, PAS `selectNode`. Le signal selectionChanged de l'arbre appelle
    // goTo, qui empile la page dans l'historique ; un seam qui appellerait
    // selectNode testerait un chemin que personne n'emprunte, et l'historique
    // resterait vide sans que rien ne le dise. C'est exactement ce que le test
    // de branchement a attrape.
    if (treeModel_) goTo(targetForNode(treeModel_->nodeForEntry(index)));
}
void LibraryHelpPage::selectTabForTest(std::size_t index) {
    if (tabs_) tabs_->setCurrentIndex(index);
}
void LibraryHelpPage::selectFieldForTest(std::size_t index) {
    selectField(static_cast<ui::RowIndex>(index));
}
void LibraryHelpPage::typeForTest(std::string text) { onDraftEdited(text); }

std::uint32_t LibraryHelpPage::benchScansForTest() const {
    if (benchPane_ == nullptr) return 0;
    return benchPane_->bench() ? benchPane_->bench()->scans() : 0;
}

void LibraryHelpPage::benchPlayForTest() {
    if (benchPane_ == nullptr || benchPane_->bench() == nullptr) return;
    benchPane_->transportForTest(1);
}
bool LibraryHelpPage::saveForTest() { return save(); }
std::string LibraryHelpPage::fieldLabelForTest(std::size_t index) const {
    return fieldModel_ ? fieldModel_->text(static_cast<ui::RowIndex>(index)) : std::string{};
}

// ---------------------------------------------------------------------------
LibraryHelpScreen::LibraryHelpScreen(std::string libsRoot, HelpScope scope)
    : menu::WidgetMenu(scope == HelpScope::Blocks ? "help.blocs" : "help.macros"),
      libsRoot_(std::move(libsRoot)), scope_(scope) {}

core::Status LibraryHelpScreen::buildUi() {
    // Lot macros 1 : les quatre onglets de l'aide au-dessus de la page.
    auto root = std::make_unique<ui::DockLayout>(scope_ == HelpScope::Blocks ? "help.blocs.root" : "help.macros.root");
    auto strip = std::make_unique<HelpTabStrip>(scope_ == HelpScope::Blocks ? HelpTabStrip::Blocks : HelpTabStrip::Macros);
    tabs_ = strip.get();
    root->dock(std::move(strip), ui::DockLayout::Side::Top, 42.f);
    connectHelpTabs(*tabs_, manager(), links_);

    auto page = std::make_unique<LibraryHelpPage>(libsRoot_, "help.macros.page", scope_);
    page_ = page.get();
    // Un lien vers l'autre onglet : on y va, la page voulue ouverte.
    page_->setOutOfScope([this](const help::Target& t) {
        help::setPendingTarget(t);
        manager().ReplaceMenu(scope_ == HelpScope::Blocks ? "help.macros" : "help.blocs");
    });
    // " Lancer " : l'ecran d'analyse ouvre le formulaire en revenant au premier plan.
    if (scope_ == HelpScope::Macros)
        page_->setMacroLauncher([this](const std::string& name) {
            help::setPendingMacroLaunch(name);
            manager().PopMenu();
        });
    root->dock(std::move(page), ui::DockLayout::Side::Center, 0.f);
    setRoot(std::move(root));
    return core::ok();
}

void LibraryHelpScreen::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    // LE POULS. Sans cet appel, "Marche" allume le bandeau et rien ne tourne :
    // un widget n'a pas d'horloge, et c'est l'ecran qui lui passe le temps de
    // l'image. Un hote qui pose la page dans son propre onglet doit faire la
    // meme chose.
    if (page_ != nullptr) page_->tick(fc.deltaSeconds);
}

void LibraryHelpScreen::onEnter() {
    // CE QUE F1 A DESIGNE. La boite aux lettres est videe en la lisant : sans
    // ca, revenir a l'aide plus tard rouvrirait la derniere page ouverte par
    // F1 au lieu de la ou l'on en etait.
    //
    // onEnter et pas buildUi : l'ecran est construit une fois, il est ENTRE a
    // chaque F1. Poser ca dans buildUi ferait marcher le premier F1 et aucun
    // des suivants - un defaut qui ne se voit qu'au deuxieme essai.
    if (page_ != nullptr && help::hasPendingTarget())
        page_->goTo(help::takePendingTarget());
}

// ---------------------------------------------------------------------------
std::string helpMenuFor(const help::Target& target, const std::string& libsRoot) {
    switch (target.kind) {
        case help::TargetKind::None:
        case help::TargetKind::Topic:   // 1.11 : le centre d'aide
            return "help";
        case help::TargetKind::Diagnostic:
        case help::TargetKind::Glossary:
            return "help.blocs";
        case help::TargetKind::LibraryEntry:
        case help::TargetKind::Parameter: {
            // Une macro est un fichier de libs/Macros ; tout le reste est un bloc.
            std::error_code ec;
            const auto mac = std::filesystem::path(libsRoot) / "Macros" / (target.entry + ".mac");
            return std::filesystem::exists(mac, ec) ? "help.macros" : "help.blocs";
        }
    }
    return "help";
}

void connectHelpTabs(HelpTabStrip& tabs, menu::MenuManager& menus, core::ConnectionScope& links) {
    links += tabs.chosen->connect([&menus](int tab) {
        menus.ReplaceMenu(std::string(HelpTabStrip::menuId(tab)));
    });
    links += tabs.closeRequested->connect([&menus] { menus.PopMenu(); });
}

} // namespace app
