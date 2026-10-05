// tests/exportask_test.cpp - Lot API 8 : les exports qui demandent ou (la fin).
//
//   exportask_test
//
//  1. Le moteur de l'IHM (hmi::Runtime) : un export parti d'un GESTE de
//     l'operateur - le bouton d'export, l'action Exporter au clic (au double
//     clic), IHM_EXPORTER dans le script qu'un clic lance - demande ou
//     (Hooks::askExport) quand l'option est cochee ; decochee, lance par un
//     timer ou par un script qui tourne seul : exports/ tout de suite. La
//     reponse (exportAnswered) : ecrit (SYS.LastExport, SYS.ExportCount,
//     l'evenement), annule, impossible (le journal). Sans question possible (pas
//     de crochet, ou il refuse) : comme avant.
//  2. L'option relue : absente (un projet d'avant) = cochee ; decochee :
//     "demander_ou=0" ; le bouton d'export d'avant (sans la propriete) demande.
//  3. ExportTarget.hpp : ce que la question nomme, le filtre, ou va le fichier
//     (exportFileFor), le silence des clics venus d'un navigateur.
//  4. Le dialogue du poste (StationExportDialog), pilote par un vrai
//     MenuManager : Entree, Echap, les grands boutons, le bouton ... (une
//     reponse de l'explorateur rangee d'avance), le kiosque.
#include "../src/app/ExportTarget.hpp"
#include "../src/app/screens/StationExportDialog.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/PathBrowse.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace hmi;
namespace fs = std::filesystem;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool journalHas(const Runtime& rt, std::string_view kind, std::string_view text) {
    for (const auto& e : rt.journal())
        if ((kind.empty() || e.kind == kind) && e.message.find(text) != std::string::npos) return true;
    return false;
}

bool eventHas(const Runtime& rt, std::string_view kind, std::string_view text) {
    for (const auto& e : rt.events())
        if ((kind.empty() || e.kind == kind) && e.message.find(text) != std::string::npos) return true;
    return false;
}

long long exportCount(Runtime& rt) {
    sim::Value v;
    (void)rt.publicRead("SYS.ExportCount", v);
    return v.asInteger();
}

Action act(Trigger t, Operation o, std::string target = {}, std::string value = {}) {
    Action a;
    a.trigger = t;
    a.operation = o;
    a.target = std::move(target);
    a.value = std::move(value);
    return a;
}

Id addObject(Project& p, View& v, Kind k, const char* name, double x, double y) {
    const Id id = edit::add(p, v, k, x, y);
    v.object(id)->name = name;
    return id;
}

Variable boolVar(Project& p, const char* name) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = "BOOL";
    v.initial = "FALSE";
    return v;
}

void click(Runtime& rt, Id object, double t) {
    rt.press(object, t);
    rt.release(object, t, true);
}

std::size_t countNamed(const std::vector<ExportRequest>& list, const std::string& name) {
    std::size_t n = 0;
    for (const auto& rq : list) n += rq.fileName == name ? 1u : 0u;
    return n;
}

// L'ecran sous le dialogue du poste (un ecran vide).
class BaseScreen final : public menu::IMenu {
public:
    core::Status Initialize() override { return core::ok(); }
    void Update(const menu::FrameContext&) override {}
    void Render(gfx::IRenderer&, const menu::FrameContext&) override {}
    ui::EventResult HandleEvent(const ui::InputEvent&) override { return ui::EventResult::Ignored; }
    void OnEnter() override {}
    void OnExit() override {}
    [[nodiscard]] menu::MenuId id() const override { return "poste"; }
    [[nodiscard]] menu::MenuTraits traits() const override { return {}; }
};

// ---------------------------------------------------------------- 1. le moteur
void runtimeAsks() {
    std::printf("1. Le moteur : la question pendant un geste, jamais seul\n");
    Project p;
    p.programs.variables.push_back(boolVar(p, "Ok"));
    p.programs.variables.push_back(boolVar(p, "Ok2"));
    p.programs.variables.push_back(boolVar(p, "Ok3"));
    View v = makeView(p, "Vue_Exports");
    v.width = 1200;
    v.height = 800;
    const Id button = addObject(p, v, Kind::ExportButton, "Export_Alarmes", 10, 10);
    v.object(button)->set("fileName", "bouton.csv");
    const Id direct = addObject(p, v, Kind::ExportButton, "Export_Direct", 10, 80);
    v.object(direct)->set("fileName", "direct.csv");
    v.object(direct)->setFlag("askWhere", false);
    const Id old = addObject(p, v, Kind::ExportButton, "Export_Ancien", 10, 150);
    v.object(old)->set("fileName", "ancien.csv");
    std::erase_if(v.object(old)->props, [](const Prop& pr) { return pr.key == "askWhere"; });   // un bouton d'avant le lot API 8
    const Id actButton = addObject(p, v, Kind::Button, "Btn_Action", 300, 10);
    v.object(actButton)->actions.push_back(act(Trigger::Click, Operation::Export, "alarmes", "action.csv"));
    const Id actDirect = addObject(p, v, Kind::Button, "Btn_Action_Direct", 300, 80);
    {
        auto a = act(Trigger::Click, Operation::Export, "alarmes", "action_direct.csv");
        a.askWhere = false;
        v.object(actDirect)->actions.push_back(a);
    }
    const Id actDouble = addObject(p, v, Kind::Button, "Btn_Double", 300, 150);
    v.object(actDouble)->actions.push_back(act(Trigger::DoubleClick, Operation::Export, "alarmes", "double.csv"));
    const Id scriptButton = addObject(p, v, Kind::Button, "Btn_Script", 600, 10);
    v.object(scriptButton)->actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Ok := IHM_EXPORTER('alarmes', 'script.csv');"));
    const Id scriptDirect = addObject(p, v, Kind::Button, "Btn_Script_Direct", 600, 80);
    v.object(scriptDirect)->actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Ok2 := IHM_EXPORTER('alarmes', 'script_direct.csv', FALSE);"));
    {
        auto timer = act(Trigger::Timer, Operation::Export, "alarmes", "minute.csv");   // coche, mais part seul
        timer.delayMs = 500;
        v.actions.push_back(timer);
    }
    // Une vue qu'un clic ouvre : son script d'ouverture et son action d'ouverture
    // exportent - ils partent avec la vue, pas du geste : sans question.
    View rapport = makeView(p, "Vue_Rapport");
    {
        Script open;
        open.id = p.allocate();
        open.name = "Ouverture";
        open.event = "OnOpen";
        open.body = "Ok3 := IHM_EXPORTER('alarmes', 'ouverture.csv');";
        rapport.scripts.push_back(open);
        rapport.actions.push_back(act(Trigger::ViewOpen, Operation::Export, "alarmes", "ouverture_action.csv"));
    }
    const Id nav = addObject(p, v, Kind::Button, "Btn_Rapport", 600, 150);
    v.object(nav)->actions.push_back(act(Trigger::Click, Operation::Navigate, "Vue_Rapport"));
    p.views = {v, rapport};
    p.config.startView = v.id;
    {
        Script sc;
        sc.id = p.allocate();
        sc.name = "Periodique";
        sc.event = "Appel";
        sc.body = "Ok3 := IHM_EXPORTER('alarmes', 'periodique.csv');";
        p.programs.scripts.push_back(sc);
    }

    std::vector<ExportRequest> written, asked;
    bool refuse = false;
    Runtime rt;
    Runtime::Hooks hooks;
    hooks.exportFile = [&](const ExportRequest& rq, std::string* where) {
        written.push_back(rq);
        if (where) *where = "exports/" + rq.fileName;
        return true;
    };
    hooks.askExport = [&](const ExportRequest& rq) {
        if (refuse) return false;
        asked.push_back(rq);
        return true;
    };
    rt.setHooks(hooks);
    rt.bind(&p, nullptr);
    rt.start(0);
    double t = 0.1;
    rt.tick(t);

    click(rt, button, t);
    check(asked.size() == 1 && asked[0].fileName == "bouton.csv" && written.empty() && asked[0].data && asked[0].format == "CSV",
          "le bouton d'export (coch\xC3\xA9 par d\xC3\xA9" "faut) : la question, rien d'\xC3\xA9" "crit encore");
    check(exportCount(rt) == 0 && rt.lastExport().empty() && journalHas(rt, "Action", "bouton.csv : o\xC3\xB9 l'\xC3\xA9" "crire"),
          "... SYS.ExportCount 0, le journal dit la question");
    rt.exportAnswered(asked[0], true, "exports/bouton.csv", t);
    check(exportCount(rt) == 1 && rt.lastExport() == "bouton.csv" && eventHas(rt, "Export", "alarmes \xE2\x86\x92 bouton.csv"),
          "Exporter : SYS.LastExport, SYS.ExportCount, l'\xC3\xA9v\xC3\xA9nement (comme avant)");

    click(rt, direct, t);
    check(written.size() == 1 && written[0].fileName == "direct.csv" && asked.size() == 1 && exportCount(rt) == 2,
          "Demander o\xC3\xB9 enregistrer d\xC3\xA9" "coch\xC3\xA9 : exports/ sans question");
    click(rt, old, t);
    check(asked.size() == 2 && asked[1].fileName == "ancien.csv", "un bouton d'avant (sans la propri\xC3\xA9t\xC3\xA9) : il demande (coch\xC3\xA9)");
    rt.exportAnswered(asked[1], false, {}, t);
    check(exportCount(rt) == 2 && journalHas(rt, "Action", "ancien.csv : annul\xC3\xA9"), "Annuler : rien d'\xC3\xA9" "crit, le journal le dit");

    click(rt, actButton, t);
    check(asked.size() == 3 && asked[2].fileName == "action.csv", "l'action Exporter au clic : la question");
    rt.exportAnswered(asked[2], true, "/srv/rapports/mon_export.csv", t);
    check(rt.lastExport() == "mon_export.csv" && eventHas(rt, "Export", "/srv/rapports/mon_export.csv"),
          "\xC3\xA9" "crit ailleurs, sous un autre nom : SYS.LastExport le nom, l'\xC3\xA9v\xC3\xA9nement le chemin");
    click(rt, actDirect, t);
    check(countNamed(written, "action_direct.csv") == 1 && asked.size() == 3, "l'action d\xC3\xA9" "coch\xC3\xA9" "e : sans question");
    rt.doubleClick(actDouble, t);
    check(asked.size() == 4 && asked[3].fileName == "double.csv", "au double clic : la question");
    rt.exportAnswered(asked[3], false, "\xC3\xA9" "criture impossible : /x/double.csv", t);
    check(journalHas(rt, "Erreur", "double.csv : \xC3\xA9" "criture impossible"), "impossible : le journal le dit (Erreur)");

    click(rt, scriptButton, t);
    check(asked.size() == 5 && asked[4].fileName == "script.csv" && rt.variable("Ok") && rt.variable("Ok")->isTruthy(),
          "IHM_EXPORTER dans le script d'un clic : la question, vrai");
    click(rt, scriptDirect, t);
    check(countNamed(written, "script_direct.csv") == 1 && asked.size() == 5 && rt.variable("Ok2")->isTruthy(),
          "IHM_EXPORTER(..., FALSE) : jamais de question");
    check(rt.callScript("Periodique", t) && countNamed(written, "periodique.csv") == 1 && asked.size() == 5,
          "un script qui tourne seul : exports/ sans question");
    for (int k = 0; k < 14; ++k) {
        t += 0.05;
        rt.tick(t);
    }
    check(countNamed(written, "minute.csv") >= 1 && asked.size() == 5, "un timer (l'option coch\xC3\xA9" "e) : sans question");
    check(rt.exportAsOperator("alarmes", "operateur.csv", {}, t, true) && asked.size() == 6 && asked[5].fileName == "operateur.csv",
          "exportAsOperator (sim-exporter) : comme un clic, la question");
    check(rt.exportAsOperator("alarmes", "operateur2.csv", {}, t, false) && countNamed(written, "operateur2.csv") == 1,
          "... sans-question : exports/");
    refuse = true;
    click(rt, button, t);
    check(countNamed(written, "bouton.csv") == 1 && asked.size() == 6, "l'\xC3\xA9" "cran ne sait pas demander (il refuse) : exports/, comme avant");
    refuse = false;
    click(rt, nav, t);
    check(countNamed(written, "ouverture.csv") == 1 && countNamed(written, "ouverture_action.csv") == 1 && asked.size() == 6,
          "une vue ouverte d'un clic : son script et son action d'ouverture exportent sans question");

    // Sans le crochet (un essai, l'aide) : tout comme avant.
    std::vector<ExportRequest> plain;
    Runtime bare;
    Runtime::Hooks h2;
    h2.exportFile = [&](const ExportRequest& rq, std::string* where) {
        plain.push_back(rq);
        if (where) *where = "exports/" + rq.fileName;
        return true;
    };
    bare.setHooks(h2);
    bare.bind(&p, nullptr);
    bare.start(0);
    bare.tick(0.1);
    click(bare, button, 0.1);
    click(bare, actButton, 0.1);
    check(plain.size() == 2 && plain[0].fileName == "bouton.csv" && plain[1].fileName == "action.csv" && exportCount(bare) == 2,
          "sans question possible : le bouton et l'action \xC3\xA9" "crivent tout de suite");
}

// ------------------------------------------------------- 2. l'option relue
void optionStored() {
    std::printf("2. L'option dans le projet (relue d'un projet d'avant : coch\xC3\xA9" "e)\n");
    Project p;
    View v = makeView(p, "Vue_Option");
    const Id b = addObject(p, v, Kind::Button, "Btn", 10, 10);
    auto off = act(Trigger::Click, Operation::Export, "alarmes", "a.csv");
    off.askWhere = false;
    v.object(b)->actions.push_back(off);
    v.object(b)->actions.push_back(act(Trigger::Click, Operation::Export, "historique", "h.xlsx"));
    const std::string text = serializeView(v);
    const auto first = text.find("demander_ou=0");
    check(first != std::string::npos && text.find("demander_ou", first + 1) == std::string::npos,
          "d\xC3\xA9" "coch\xC3\xA9" "e : demander_ou=0 ; coch\xC3\xA9" "e : rien d'\xC3\xA9" "crit (les fichiers d'avant ne changent pas)");
    auto back = parseView(text);
    check(back && back->object(b) && back->object(b)->actions.size() == 2 && !back->object(b)->actions[0].askWhere
              && back->object(b)->actions[1].askWhere && back->object(b)->actions == v.object(b)->actions,
          "relue : \xC3\xA0 l'identique");
    std::string before = text;
    before.replace(first - 1, std::string(" demander_ou=0").size(), "");   // la meme ligne, ecrite avant le lot API 8
    auto older = parseView(before);
    check(older && older->object(b) && older->object(b)->actions[0].askWhere, "une action d'un projet d'avant : coch\xC3\xA9" "e");
    check(describeAction(off).find("(sans question)") != std::string::npos
              && describeAction(act(Trigger::Click, Operation::Export)).find("sans question") == std::string::npos,
          "la liste des actions le dit : \xC2\xAB (sans question) \xC2\xBB");
    const Object eb = makeObject(Kind::ExportButton, 1, "E", 0, 0, 1);
    check(eb.flag("askWhere", false), "un bouton d'export neuf : Demander o\xC3\xB9 enregistrer coch\xC3\xA9");
}

// ------------------------------------------------------- 3. ExportTarget.hpp
void targetHelpers() {
    std::printf("3. ExportTarget.hpp : la question, le filtre, o\xC3\xB9 va le fichier\n");
    check(app::exportWhatOf("alarmes", "CSV") == "les alarmes (CSV)" && app::exportWhatOf("recette:Gaz", "Excel") == "la recette Gaz (Excel)"
              && app::exportWhatOf("objet:Courbe_1", "PDF") == "l'objet Courbe_1 (PDF)"
              && app::exportWhatOf("\xC3\xA9v\xC3\xA9nements", "CSV") == "les \xC3\xA9v\xC3\xA9nements (CSV)"
              && app::exportWhatOf("rapport:Journalier", "PDF") == "le rapport Journalier (PDF)",
          "ce que la question nomme : les alarmes (CSV), la recette Gaz (Excel), l'objet Courbe_1 (PDF)");
    check(app::exportFilterOf("Excel") == "Classeurs Excel|*.xlsx" && app::exportFilterOf("PDF") == "Documents PDF|*.pdf"
              && app::exportFilterOf("CSV") == "Fichiers CSV|*.csv",
          "le filtre de l'explorateur, selon le format");
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "xpg_exportask";
    fs::remove_all(base, ec);
    const fs::path folder = base / "exports";
    check(app::exportFileFor(folder, "a.csv", "") == folder / "a.csv" && fs::is_directory(folder, ec), "rien de choisi : exports/a.csv (cr\xC3\xA9\xC3\xA9)");
    check(app::exportFileFor(folder, "a.csv", "autre.csv") == folder / "autre.csv", "un nom seul : dans exports/");
    check(app::exportFileFor(folder, "a.csv", "sous/") == folder / "sous" / "a.csv" && fs::is_directory(folder / "sous", ec),
          "un dossier tap\xC3\xA9 (sous/) : le nom habituel dedans, le dossier cr\xC3\xA9\xC3\xA9");
    check(app::exportFileFor(folder, "a.csv", app::exportUtf8Of(base / "ailleurs") + "/") == base / "ailleurs" / "a.csv",
          "un autre dossier : le nom habituel dedans");
    fs::create_directories(base / "dossier", ec);
    check(app::exportFileFor(folder, "a.csv", app::exportUtf8Of(base / "dossier")) == base / "dossier" / "a.csv",
          "un dossier qui existe, sans / au bout : dedans aussi");
    check(app::exportFileFor(folder, "a.csv", "  \"" + app::exportUtf8Of(base / "x" / "mon") + "\" ") == base / "x" / "mon.csv",
          "un fichier sans extension, entre guillemets : mon.csv (l'extension du format)");
    fs::remove_all(base, ec);
    check(!app::exportQuestionsMuted(), "par d\xC3\xA9" "faut : on demande");
    {
        const app::ExportQuestionMute web;
        {
            const app::ExportQuestionMute again;
            check(app::exportQuestionsMuted(), "un clic venu d'un navigateur : pas de question");
        }
        check(app::exportQuestionsMuted(), "... jusqu'au bout du clic");
    }
    check(!app::exportQuestionsMuted(), "... puis on demande de nouveau");
}

// ------------------------------------------------ 4. le dialogue du poste
struct DialogRun {
    menu::MenuManager       mm{menu::MenuFactory{}};
    ui::Theme               theme = ui::Theme::dark();
    app::StationExportDialog* dialog{nullptr};
    bool                    answered{false};
    menu::DialogResult      result;

    explicit DialogRun(app::StationExportDialog::Spec spec) {
        mm.PushMenu(std::make_unique<BaseScreen>());
        mm.applyPending();
        auto d = std::make_unique<app::StationExportDialog>(std::move(spec));
        dialog = d.get();
        mm.ShowDialog(std::move(d), [this](const menu::DialogResult& r) {
            answered = true;
            result = r;
        });
        mm.applyPending();
        frame();
    }
    void frame() {
        const menu::FrameContext fc{0.016, 1.0, &theme, {1280.f, 800.f}, 1.f};
        mm.Update(fc);
    }
    void send(const ui::InputEvent& e) {
        (void)mm.HandleEvent(e);
        mm.applyPending();
    }
    void press(ui::Widget& w) {
        const auto b = w.bounds();
        const gfx::Point c{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
        send(ui::MouseMove{c, {}, {}});
        send(ui::MouseDown{c, ui::MouseButton::Left, 1, {}});
        send(ui::MouseUp{c, ui::MouseButton::Left, {}});
    }
};

app::StationExportDialog::Spec stationSpec() {
    app::StationExportDialog::Spec s;
    s.what = "les alarmes (CSV)";
    s.proposed = "/projets/gaz/exports/alarmes_2026-09-29.csv";
    s.browse = ui::saveFile("Fichiers CSV|*.csv", s.proposed, "Exporter les alarmes (CSV)");
    return s;
}

void stationDialog() {
    std::printf("4. Le dialogue du poste, au doigt\n");
    {
        DialogRun run(stationSpec());
        auto* d = run.dialog;
        check(d->title() == "Exporter les alarmes (CSV)" && d->field() && d->field()->text() == "/projets/gaz/exports/alarmes_2026-09-29.csv",
              "le titre, le chemin propos\xC3\xA9 (exports/, le nom habituel)");
        check(d->exportButton() && d->cancelButton() && d->exportButton()->text() == "Exporter" && d->cancelButton()->text() == "Annuler"
                  && d->exportButton()->bounds().h >= 80.f && d->cancelButton()->bounds().h >= 80.f && d->exportButton()->bounds().w >= 200.f,
              "Exporter et Annuler : de grands boutons (80 px de haut au moins)");
        check(d->browseButton() && d->browseButton()->enabled() && d->browseButton()->bounds().h >= 70.f
                  && d->browseButton()->fieldLabel() == app::StationExportDialog::kFieldLabel && d->field()->bounds().h >= 70.f,
              "le champ et le bouton \xE2\x80\xA6 : grands aussi ; parcourir \"Dossier ou fichier\" le trouve");
        const auto f = d->field()->bounds(), bb = d->browseButton()->bounds(), ok = d->exportButton()->bounds(), no = d->cancelButton()->bounds();
        check(bb.x >= f.x + f.w && ok.x >= no.x + no.w && ok.y > f.y + f.h && f.x >= 0.f && ok.x + ok.w <= 1280.f,
              "le bouton \xE2\x80\xA6 \xC3\xA0 droite du champ ; Annuler puis Exporter, dessous, dans l'\xC3\xA9" "cran");
        run.send(ui::KeyDown{ui::Key::Return, {}, false});
        check(run.answered && run.result.accepted() && run.result.payload == "/projets/gaz/exports/alarmes_2026-09-29.csv",
              "Entr\xC3\xA9" "e : Exporter, le chemin propos\xC3\xA9");
    }
    {
        DialogRun run(stationSpec());
        run.send(ui::KeyDown{ui::Key::Escape, {}, false});
        check(run.answered && !run.result.accepted() && run.mm.depth() == 1, "\xC3\x89" "chap : Annuler (le poste reste)");
    }
    {
        DialogRun run(stationSpec());
        run.press(*run.dialog->cancelButton());
        check(run.answered && !run.result.accepted(), "un toucher sur Annuler");
    }
    {
        DialogRun run(stationSpec());
        ui::queueFilePick("/cle_usb/rapports/nuit");        // la reponse de l'explorateur, rangee d'avance
        check(run.dialog->browseButton()->browse() && run.dialog->field()->text() == "/cle_usb/rapports/nuit.csv",
              "le bouton \xE2\x80\xA6 : l'explorateur ; le chemin choisi dans le champ (l'extension ajout\xC3\xA9" "e)");
        run.frame();
        run.press(*run.dialog->exportButton());
        check(run.answered && run.result.accepted() && run.result.payload == "/cle_usb/rapports/nuit.csv", "un toucher sur Exporter : l\xC3\xA0 o\xC3\xB9 on l'a choisi");
    }
    {
        auto spec = stationSpec();
        spec.browseDenied = "Kiosque : r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'administrateur du poste";
        spec.fixedPath = true;
        DialogRun run(std::move(spec));
        check(!run.dialog->browseButton()->enabled() && !run.dialog->browseButton()->browse(),
              "le kiosque, sans l'administrateur : le bouton \xE2\x80\xA6 gris\xC3\xA9 (l'explorateur ne s'ouvre pas)");
        run.press(*run.dialog->field());
        run.send(ui::TextInput{"x"});
        check(run.dialog->field()->text() == "/projets/gaz/exports/alarmes_2026-09-29.csv", "... et le chemin ne se tape pas");
        run.send(ui::KeyDown{ui::Key::Return, {}, false});
        check(run.answered && run.result.accepted() && run.result.payload == "/projets/gaz/exports/alarmes_2026-09-29.csv",
              "... Exporter : dans exports/");
    }
}

} // namespace

int main() {
    runtimeAsks();
    optionStored();
    targetHelpers();
    stationDialog();
    std::printf(failures ? "ECHEC : %d echec(s)\n" : "exportask_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
