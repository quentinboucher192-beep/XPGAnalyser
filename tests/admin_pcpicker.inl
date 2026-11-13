// =============================================================================
//  tests/admin_pcpicker.inl - 1.51.0 : << AJOUTER UN ADMIN >> EN UNE ETAPE, ET LA
//  RECHERCHE DU PC (license_admin/gui/PcPicker), sans reseau - inclus par
//  admin_gui_admins_test.cpp (testPcPicker).
// -----------------------------------------------------------------------------
//  Des centaines de PC fictifs (<< PC-ESSAI-042 >>, des clients << Atelier d'essai
//  n >>, << Helene Essai >>), jamais un vrai client :
//   - l'ordre : les plus recemment actifs d'abord ; le premier choisi ; << 300 PC >> ;
//   - la recherche : le nom du PC, le client (sans accents : << helene >>), la
//     version du Studio, << aujourd'hui >>, l'identifiant, un mot exclu (-mot) ;
//     << 3 sur 300 PC >> ; le choix garde s'il est encore montre, sinon le premier ;
//     rien ne correspond : << aucun sur 300 >>, aucun choix ; le surlignage ;
//   - le clavier (dans le champ) : Bas, Haut, Page bas, Entree (picked), Echap
//     (efface la recherche) ; la souris : un clic choisit, la molette defile ;
//   - vite : 200 recherches sur 1 000 PC, bien moins d'une seconde ;
//   - le dialogue << Ajouter un admin >> : une seule etape - le PC (la recherche),
//     son nom, son profil ; plus de partie 2 ni 3 (ni empreinte, ni 6 chiffres, ni
//     Accepter, ni Refuser, ni Plus tard).
// =============================================================================
#include "../src/license_admin/gui/PcPicker.hpp"

#include <chrono>

namespace {

std::vector<gui::MessagePc> fakePcs(std::size_t n, lic::Seconds now) {
    std::vector<gui::MessagePc> out;
    for (std::size_t i = 0; i < n; ++i) {
        gui::MessagePc p;
        char id[33];
        std::snprintf(id, sizeof id, "%08zx%024zx", i * 2654435761u % 0xFFFFFFFFu, i);
        p.deviceId = id;
        p.deviceKey = std::string(64, 'a');
        char name[32];
        std::snprintf(name, sizeof name, "PC-ESSAI-%03zu", i);
        p.name = name;
        p.customer = i == 7 ? std::string("H\xC3\xA9l\xC3\xA8ne Essai") : "Atelier d'essai " + std::to_string(i % 40);
        p.licenseId = "XPG-ESSAI-" + std::to_string(1000 + i);
        p.studioVersion = i % 3 == 0 ? "1.50.1" : "1.49.0";
        p.studio = (i % 2 ? "IHM Studio " : "API Studio ") + p.studioVersion;
        // les plus recents : le 5 (aujourd'hui), puis le 250 (hier), puis l'ordre des index a rebours ; le 9 : jamais vu
        p.lastSeen = i == 5 ? now - 600 : i == 250 ? now - 86400 - 60 : i == 9 ? 0 : now - 3 * 86400 - static_cast<lic::Seconds>(i) * 3600;
        out.push_back(std::move(p));
    }
    return out;
}

void testPcPicker(const std::string& source) {
    std::printf("-- Ajouter un admin : une etape ; la recherche du PC (PcPicker, 300 PC fictifs)\n");
    const lic::Seconds now = g_now;
    gui::PcPicker picker("essai.pc");
    picker.setPcs(fakePcs(300, now), now);
    TextRenderer r;
    std::string t = paint(picker, r, 700.f, 420.f);
    check(picker.shown().size() == 300 && picker.countText() == "300 PC" && has(t, "300 PC"), "300 PC, tous montres (" + picker.countText() + ")");
    check(picker.selected() && picker.selected()->name == "PC-ESSAI-005" && picker.pcs()[static_cast<std::size_t>(picker.shown()[1])].name == "PC-ESSAI-250",
          "les plus recemment actifs d'abord (aujourd'hui, hier...) ; le premier choisi");
    check(picker.pcs()[static_cast<std::size_t>(picker.shown().back())].name == "PC-ESSAI-009" && picker.lastSeenText(*picker.selected()) == "aujourd'hui" &&
              picker.lastSeenText(picker.pcs()[9]) == "jamais vu" && picker.lastSeenText(picker.pcs()[250]) == "hier",
          "jamais vu : a la fin ; << aujourd'hui >>, << hier >>, << jamais vu >>");
    check(has(t, "PC-ESSAI-005") && has(t, "aujourd'hui") && has(t, "Atelier d'essai 5") && has(t, "API Studio 1.49.0"), "la ligne : le PC, sa derniere activite, son client, son Studio");
    picker.setQuery("essai-01");
    check(picker.shown().size() == 10 && picker.countText() == "10 sur 300 PC", "<< essai-01 >> : 10 PC (" + picker.countText() + ")");
    picker.setQuery("helene");
    check(picker.shown().size() == 1 && picker.selected() && picker.selected()->name == "PC-ESSAI-007", "<< helene >> : sans accents, le client H\xC3\xA9l\xC3\xA8ne Essai");
    picker.setQuery("atelier 12 1.50.1");
    std::size_t ok = 0;
    for (const int i : picker.shown()) ok += picker.pcs()[static_cast<std::size_t>(i)].studioVersion == "1.50.1" && picker.pcs()[static_cast<std::size_t>(i)].customer == "Atelier d'essai 12";
    check(!picker.shown().empty() && ok == picker.shown().size(), "deux mots et une version : chaque terme quelque part (" + picker.countText() + ")");
    picker.setQuery("aujourd'hui");
    check(picker.shown().size() == 1 && picker.selected()->name == "PC-ESSAI-005", "<< aujourd'hui >> : la derniere activite se cherche");
    picker.setQuery(picker.pcs()[42].deviceId.substr(0, 10));
    check(picker.selected() && picker.selected()->name == "PC-ESSAI-042", "le debut de son identifiant");
    picker.setQuery("atelier -essai-0");
    bool none0 = true;
    for (const int i : picker.shown()) none0 = none0 && picker.pcs()[static_cast<std::size_t>(i)].name.rfind("PC-ESSAI-0", 0) != 0;
    check(!picker.shown().empty() && none0, "un mot exclu (-essai-0)");
    picker.setQuery("essai-2");
    const auto marks = picker.highlights("PC-ESSAI-250");
    check(marks.size() == 1 && marks[0].first == 3 && marks[0].second == 10, "le surlignage : << ESSAI-2 >> dans PC-ESSAI-250");
    // le choix garde quand il est encore montre ; sinon le premier
    check(picker.select(picker.pcs()[250].deviceId) && picker.selected()->name == "PC-ESSAI-250", "choisir PC-ESSAI-250");
    picker.setQuery("essai-25");
    check(picker.selected() && picker.selected()->name == "PC-ESSAI-250", "la recherche affinee : le choix reste");
    picker.setQuery("zzz introuvable");
    t = paint(picker, r, 700.f, 420.f);
    check(picker.shown().empty() && !picker.selected() && picker.selectedValue().empty() && picker.countText() == "aucun sur 300" && has(t, "Aucun PC ne correspond"),
          "rien ne correspond : aucun choix, dit");
    // le clavier, dans le champ
    picker.setQuery({});
    (void)paint(picker, r, 700.f, 420.f);
    const gfx::Rect fb = picker.field().bounds();
    (void)picker.dispatch(ui::InputEvent{ui::MouseDown{{fb.x + 20.f, fb.y + 10.f}, ui::MouseButton::Left, 1, {}}});
    check(picker.field().focused(), "un clic dans le champ : il a le clavier");
    const auto key = [&](ui::Key k) { return picker.dispatch(ui::InputEvent{ui::KeyDown{k, {}, false}}); };
    check(key(ui::Key::Down) == ui::EventResult::Consumed && picker.selected()->name == "PC-ESSAI-250", "Bas : le suivant (hier)");
    check(key(ui::Key::Up) == ui::EventResult::Consumed && picker.selected()->name == "PC-ESSAI-005", "Haut : le precedent");
    (void)key(ui::Key::PageDown);
    check(picker.selected() && picker.selected()->name != "PC-ESSAI-005" && picker.firstVisible() >= 0, "Page bas : une page plus loin, la liste suit");
    int picked = 0;
    core::ConnectionScope links;
    links += picker.picked->connect([&] { ++picked; });
    (void)key(ui::Key::Return);
    check(picked == 1, "Entree : le PC pris (picked)");
    picker.field().setText("essai-1");
    (void)key(ui::Key::Escape);
    check(picker.query().empty() && picker.shown().size() == 300, "Echap : la recherche effacee, tout revient");
    // la souris : un clic choisit une ligne, la molette defile
    t = paint(picker, r, 700.f, 420.f);
    const gfx::Rect pb = picker.bounds();
    (void)picker.dispatch(ui::InputEvent{ui::MouseWheel{{pb.x + 100.f, pb.y + 200.f}, 0.f, -1.f, {}}});
    check(picker.firstVisible() == 3, "la molette : 3 lignes plus bas");
    (void)picker.dispatch(ui::InputEvent{ui::MouseDown{{pb.x + 100.f, pb.y + 34.f + 8.f + 46.f + 10.f}, ui::MouseButton::Left, 1, {}}});
    check(picker.selected() && picker.selected() == &picker.pcs()[static_cast<std::size_t>(picker.shown()[4])], "un clic : la ligne choisie (la 5e, apres la molette)");
    // vite : 1 000 PC, 200 recherches
    {
        gui::PcPicker big("essai.pc.grand");
        const auto t0 = std::chrono::steady_clock::now();
        big.setPcs(fakePcs(1000, now), now);
        const char* qs[] = {"atelier 3", "essai-9", "1.50.1", "helene", "-atelier", "aujourd'hui", "api studio 1.49", "zzz", "pc", "xpg-essai-15"};
        std::size_t total = 0;
        for (int k = 0; k < 200; ++k) {
            big.setQuery(qs[k % 10]);
            total += big.shown().size();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(ms < 1500.0 && total > 0, "1 000 PC, 200 recherches : " + std::to_string(static_cast<int>(ms)) + " ms (bien moins d'une seconde attendu)");
    }
    // le dialogue : une seule etape
    {
        Fixture fx(source, "admins-ajouter-une-etape");
        check(fx.ready(), "le poste de l'admin+ (cle d'essai)");
        HostRig rig(fx);
        auto dlg = gui::makeAddAdminDialog(rig.host());
        (void)dlg->Initialize();
        ui::Widget& root = *dlg->widgetRoot();
        TextRenderer dr;
        const std::string d = paintMenu(*dlg, dr, 1400.f, 900.f);
        auto* pc = find<gui::PcPicker>(root, "admins.ajouter.pc");
        check(pc && pc->visible() && root.findById("admins.ajouter.pc.recherche"), "le PC de son Studio : la recherche (PcPicker)");
        check(has(d, "Ajouter un admin") && has(d, "Le PC de son Studio") && has(d, "Son profil") && has(d, "d\xC3\xA8s l'installation de XPG Admin"),
              "une etape : son nom, son profil, le PC ; l'invitation fait l'admin");
        check(!has(d, "Il installe l'Admin") && !has(d, "Tu v\xC3\xA9rifies l'empreinte") && !has(d, "6 chiffres") && !has(d, "Relever maintenant") &&
                  !has(d, "Ce que voit le futur admin") && !has(d, "CE QUE VOIT"),
              "plus de partie 2 (il installe, sa demande) ni 3 (l'empreinte, les 6 chiffres)");
        for (const char* gone : {"admins.accepter", "admins.refuser", "admins.plus-tard", "admins.empreinte.verifiee", "admins.confirmation", "admins.ajouter.relever"})
            check(root.findById(gone) == nullptr, std::string("plus de << ") + gone + " >>");
        auto* send = find<ui::Button>(root, "admins.ajouter.envoyer");
        check(send && send->visible() && !send->enabled() && shown(root, "admins.ajouter.annuler"), "Envoyer l'invitation : grise sans son nom ; Annuler");
        (void)type(root, "admins.ajouter.nom", "Nina Essai");
        (void)paintMenu(*dlg, dr, 1400.f, 900.f);
        check(send && send->enabled() == (pc && pc->selected() != nullptr), "son nom tape : Envoyer l'invitation des qu'un PC est choisi");
    }
}

} // namespace
