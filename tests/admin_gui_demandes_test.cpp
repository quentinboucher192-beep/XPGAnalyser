// =============================================================================
//  tests/admin_gui_demandes_test.cpp - 1.35.0 : LA PAGE << Demandes en ligne >>
//  de l'application de l'editeur, dessinee a blanc, par le SERVICE DES
//  LICENCES (1.34.0 : la passerelle GitHub, retiree)
// -----------------------------------------------------------------------------
//  Sans reseau : le service pas regle (la page dit quoi faire, << Regler le
//  service >> mene a Service en ligne) ; regle, jamais releve ; des demandes
//  rangees comme un releve les range (processRequests) : la table (client, PC,
//  logiciel, licence demandee, etat), une demande d'ajout a une licence
//  existante << a verifier >> (la table, la page, l'assistant), une demande
//  illisible (scellee pour une autre cle), le filtre, le tableau de bord ;
//  Accepter... (l'assistant avec la demande, << Signer et publier >>),
//  Accepter en ligne... (l'assistant d'une licence en ligne qui y repond), un
//  travail en cours (les boutons attendent).
//  Contre le VRAI service local (tests/service_local.hpp ; sans Node ni
//  wrangler : SAUTE) : des PC deposent leurs demandes (POST /v1/demandes,
//  scellees pour l'editeur, signees par le PC) ; Relever ; Accepter (la
//  licence liee publiee : le PC lit << acceptee >> et SA licence, scellee pour
//  lui) ; un ajout a une licence existante (la case a cocher, l'identifiant
//  jamais en clair) ; Accepter en ligne (le code montre une fois, dans le
//  dialogue seulement ; un autre PC s'active avec lui ; le PC de la demande lit
//  << acceptee >> sans le code) ; Refuser (la raison scellee pour le PC) ; une
//  revocation publiee (la liste scellee pour le PC, jamais publique) ; hors
//  ligne : dit. Le kit des Studios refuse une cle d'essai.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace admintest;
namespace gw = licensing::gateway;
namespace sv = licensing::service;
namespace online = license_admin::online;
namespace json = core::json;

namespace {

const std::string kFakeUrl = "https://xpg-licences.essai.workers.dev";   // jamais appele (les sections sans reseau)

// Le texte dessine, a plat (les lignes coupees remises bout a bout).
std::string flat(std::string t) {
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

std::string paintPage(ui::Widget& w, float width = 1280.f, float height = 760.f) {
    TextRenderer r;
    return flat(paint(w, r, width, height));
}

std::string paintDialog(menu::IMenu& m) {
    TextRenderer r;
    return flat(paintMenu(m, r, 1280.f, 800.f));
}

bool shown(ui::Widget& root, std::string_view id) {
    ui::Widget* w = root.findById(id);
    return w && w->visible();
}

int rowOf(ui::TableView& t, std::size_t col, const std::string& text) {
    const auto& m = t.model();
    if (!m) return -1;
    for (std::size_t r = 0; r < m->rowCount(); ++r)
        if (m->cellText(static_cast<ui::RowIndex>(r), col).find(text) != std::string::npos) return static_cast<int>(r);
    return -1;
}

std::size_t rows(ui::Widget& root, std::string_view id) {
    auto* t = find<ui::TableView>(root, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}

bool next(ui::Widget& w) { return click(w, "wizard.suivant"); }

std::string hex32() {
    std::uint8_t b[16];
    lic::crypto::randomBytes(b, sizeof b);
    return lic::crypto::hex(b, sizeof b);
}

// ---- un PC d'essai : sa cle, ses demandes, ses appels signes (comme un Studio) ----
struct Pc {
    std::unique_ptr<lic::SigningKey> key;
    std::string                      name, nonce;
    int                              number{0};   // le numero de sa demande sur le service
    Pc(const fs::path& dir, const std::string& n) : name(n) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::string why;
        key = lic::SigningKey::create(U(dir / "identite-pc.cle"), lic::deviceKeySpec(), n, false, &why);
        if (!key) std::printf("  (cle du PC : %s)\n", why.c_str());
    }
    [[nodiscard]] std::string device() const { return key ? sv::deviceId(key->publicKey()) : std::string(); }
    // Une demande d'activation signee par ce PC.
    [[nodiscard]] std::string request(const std::string& licenseId, lic::Seconds at) {
        if (!key) return {};
        lic::ActivationRequest r;
        r.deviceKey = key->publicKeyHex();
        r.deviceName = name;
        r.licenseId = licenseId;
        r.software = "API Studio 1.35.0 (essai)";
        r.createdAt = at;
        r.nonce = hex32();
        r.station = lic::stationHash("machine d'essai " + name, key->publicKey());   // le service l'exige (activer)
        nonce = r.nonce;
        const auto b = lic::signBlock(lic::DocKind::Request, lic::writeRequest(r), key->publicKey(), [this](std::string_view m) { return key->sign(m); });
        return lic::writeBlock(b);
    }
};

// La demande manuelle, scellee pour l'editeur (ce que le Studio depose).
std::string sealedRequest(const lic::crypto::PublicKey& editor, const std::string& requestText, const std::string& customer, const std::string& message) {
    gw::OnlineRequest o;
    o.request = requestText;
    o.customer = customer;
    o.contact = "essai@exemple.invalid";
    o.message = message;
    return gw::sealFor(editor, gw::writeOnlineRequest(o));
}

// Une entree de la liste des demandes, telle que le service la rend (GET /admin/demandes).
json::Value demande(int number, const Pc& pc, const std::string& keyId, const std::string& sealed) {
    json::Value d = json::Value::object();
    d.set("numero", number);
    d.set("poste", pc.device());
    d.set("cle", keyId);
    d.set("scelle", sealed);
    d.set("etat", "attente");
    d.set("cree", lic::isoTime(g_now));
    d.set("maj", lic::isoTime(g_now));
    return d;
}

struct Reply {
    int         status{0};
    std::string body;
    json::Value value;
};

// Un appel au service (signe par `key` si donnee, a l'heure du service).
Reply call(const std::string& base, const std::string& method, const std::string& path, const std::string& body, const lic::SigningKey* key) {
    lic::Seconds now = lic::systemNow();
    if (key) {
        auto c = app::ai::makeHttpClient();
        app::ai::HttpRequest q;
        q.method = "GET";
        q.url = base + "/v1/etat";
        std::string b;
        app::ai::HttpSink s;
        s.onData = [&b](std::string_view x) { b.append(x); };
        (void)c->perform(q, s);
        json::Value v;
        if (json::parse(b, v, nullptr) && v.isObject()) now = static_cast<lic::Seconds>(v.integer("heure", now));
    }
    auto client = app::ai::makeHttpClient();
    app::ai::HttpRequest req;
    req.method = method;
    req.url = base + path;
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    if (key)
        for (auto& h : sv::signRequest(method, path, body, key->publicKey(), [key](std::string_view m) { return key->sign(m); }, now)) req.headers.push_back(h);
    Reply out;
    app::ai::HttpSink sink;
    sink.onData = [&out](std::string_view chunk) { out.body.append(chunk.data(), chunk.size()); };
    const auto r = client->perform(req, sink);
    out.status = r.status;
    (void)json::parse(out.body, out.value, nullptr);
    return out;
}

// Le PC depose sa demande (POST /v1/demandes) ; son numero.
bool post(const std::string& base, Pc& pc, const lic::SigningKey& editor, const std::string& licenseId, const std::string& customer) {
    json::Value b = json::Value::object();
    b.set("cle", editor.keyId());
    b.set("scelle", sealedRequest(editor.publicKey(), pc.request(licenseId, g_now - 30), customer, "Message d'essai de " + pc.name));
    const Reply d = call(base, "POST", "/v1/demandes", json::write(b, false), pc.key.get());
    pc.number = static_cast<int>(d.value.integer("numero"));
    return d.status == 201 && pc.number > 0;
}

// L'etat << poste >> d'un PC (GET /v1/poste) : le bloc signe par l'editeur, son contenu ouvert par le PC.
bool posteState(const std::string& base, const Pc& pc, const lic::SigningKey& editor, gw::DeviceStatus& st, gw::StatusContent& content) {
    const Reply r = call(base, "GET", "/v1/poste", {}, pc.key.get());
    if (r.status != 200 || !r.value.isObject() || !r.value.find("poste") || !r.value.at("poste").isString()) return false;
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(r.value.at("poste").asString(), blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::DeviceStatus) return false;
    if (!lic::verifyBlock(blocks[0], editor.publicKey())) return false;
    if (!gw::parseDeviceStatus(blocks[0].document, st, nullptr)) return false;
    content = gw::StatusContent{};
    if (st.sealed.empty()) return true;
    std::string plain;
    return gw::openWith(*pc.key, st.sealed, plain, nullptr) && gw::parseStatusContent(plain, content, nullptr);
}

// L'identifiant de la licence d'un fichier .xpglic.
std::string licenceIdOf(const std::string& text) {
    std::vector<lic::SignedBlock> blocks;
    lic::License l;
    if (!lic::readBlocks(text, blocks, nullptr)) return {};
    for (const auto& b : blocks)
        if (b.kind == lic::DocKind::License && lic::parseLicense(b.document, l, nullptr)) return l.licenseId;
    return {};
}

void startBusy(HostRig& rig) {
    rig.bg.start("essai occupe", [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        return gui::OpResult::done("fini");
    });
}
void endBusy(HostRig& rig) {
    rig.bg.wait();
    (void)rig.bg.take();
}

// Choisir la ligne d'une table dont la colonne contient `text`.
bool selectRow(ui::Widget& root, std::size_t col, const std::string& text) {
    auto* t = find<ui::TableView>(root, "demandes.table");
    if (!t) return false;
    const int r = rowOf(*t, col, text);
    if (r < 0) return false;
    t->selectModelRows({static_cast<ui::RowIndex>(r)});
    return true;
}

// L'assistant ouvert : jusqu'a la derniere etape (<< Signer... >>).
bool walkToSign(menu::IMenu& d, bool chooseEdition) {
    ui::Widget& w = *d.widgetRoot();
    (void)paintDialog(d);
    if (!next(w)) return false;   // 2. client
    (void)paintDialog(d);
    if (!next(w)) return false;   // 3. edition
    if (chooseEdition) {
        auto* eds = find<ui::TableView>(w, "wizard.editions");
        if (!eds) return false;
        const int pro = rowOf(*eds, 0, "PROFESSIONAL");
        if (pro < 0) return false;
        eds->selectModelRows({static_cast<ui::RowIndex>(pro)});
    }
    (void)paintDialog(d);
    if (!next(w)) return false;   // 4. fonctionnalites
    (void)paintDialog(d);
    if (!next(w)) return false;   // 5. recapitulatif
    (void)paintDialog(d);
    return next(w);               // 6. signer
}

// Un controle sur un texte dessine ; XPG_DUMP=1 : le texte est montre s'il echoue.
void checkText(bool ok, const std::string& what, const std::string& text) {
    check(ok, what);
    if (!ok && std::getenv("XPG_DUMP")) std::printf("---- %s ----\n%s----\n", what.c_str(), text.c_str());
}

std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(aucun rapport)"); }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_demandes_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    g_now = lic::systemNow();   // le service local a l'heure du PC : le poste d'essai aussi

    // ================================================================================== sans reseau
    Fixture f0(source, "demandes-0");
    check(f0.ready(), "le poste d'essai est pret (cle d'essai, registre)");
    const lic::SigningKey* editor0 = f0.ws->signingKey();
    if (!editor0) return finish();
    const std::string shared = "XPG-2026-00201";
    check(f0.licence(shared, "Client d'essai Beta", "PROFESSIONAL", {f0.request("pc-x", "Poste essai X")}).ok, "une licence existante (par fichier) pour Beta");
    HostRig rig0(f0);
    auto h0 = rig0.host();

    std::printf("-- le service pas regle\n");
    auto page0 = gui::makeRequestsPage(h0);
    {
        const std::string t = paintPage(*page0);
        check(has(t, "Demandes en ligne") && page0->title() == "Demandes en ligne", "le titre");
        checkText(has(t, "pas encore r\xC3\xA9gl\xC3\xA9") && has(t, "Service en ligne") && has(t, "Noter des demandes"), "la page dit quoi faire (et le chemin par fichier)", t);
        check(shown(*page0, "demandes.regler") && !shown(*page0, "demandes.relever") && !shown(*page0, "demandes.accepter") &&
                  !shown(*page0, "demandes.accepter-en-ligne") && !shown(*page0, "demandes.refuser"),
              "seulement << Regler le service >>");
        check(click(*page0, "demandes.regler") && !rig0.navigations.empty() && rig0.navigations.back() == "service", "Regler -> Service en ligne");
        check(page0->findById("demandes.github") == nullptr, "plus de << Voir sur GitHub >>");
    }

    std::printf("-- regle, jamais releve (sans reseau)\n");
    {
        check(f0.ws->serviceConfigure(kFakeUrl).ok, "le service regle");
        page0->refresh();
        const std::string t = paintPage(*page0);
        check(has(t, "jamais relev\xC3\xA9" "es") && has(t, "Pas encore relev\xC3\xA9" "es"), "jamais relevees : dit");
        check(shown(*page0, "demandes.relever") && shown(*page0, "demandes.accepter") && !shown(*page0, "demandes.regler"), "Relever, Accepter...");
        check(!find<ui::Button>(*page0, "demandes.accepter")->enabled() && !find<ui::Button>(*page0, "demandes.refuser")->enabled(), "sans demande : grises");
    }

    std::printf("-- des demandes rangees (processRequests, comme un releve) : la table, a verifier, illisible, le filtre\n");
    Pc a0(f0.root / "pc-a", "Poste essai A"), b0(f0.root / "pc-b", "Poste essai B"), x0(f0.root / "pc-z", "Poste essai Z");
    {
        // Z scelle pour une autre cle que celle de l'editeur (la sienne) : illisible ici.
        const lic::crypto::PublicKey other = x0.key ? x0.key->publicKey() : lic::crypto::PublicKey{};
        json::Value arr = json::Value::array();
        arr.push(demande(1, a0, editor0->keyId(), sealedRequest(editor0->publicKey(), a0.request({}, g_now - 60), "Client d'essai Gamma", "Message d'essai de A")));
        arr.push(demande(2, b0, editor0->keyId(), sealedRequest(editor0->publicKey(), b0.request(shared, g_now - 50), "Client d'essai Beta", "Un poste de plus")));
        arr.push(demande(3, x0, editor0->keyId(), sealedRequest(other, x0.request({}, g_now - 40), "Client d'essai Z", "")));
        json::Value list = json::Value::object();
        list.set("demandes", arr);
        online::Options o;
        o.now = g_now;
        o.tool = "admin_gui_demandes_test";
        const online::Result r = online::processRequests(U(f0.admin), *editor0, list, kFakeUrl, o);
        check(r.ok, "rangees : " + r.text());
        f0.ws->reload();
        page0->refresh();
        const std::string t = paintPage(*page0, 2000.f, 760.f);
        check(rows(*page0, "demandes.table") == 2, "deux en attente (l'illisible n'attend rien)");
        check(has(t, "Client d'essai Gamma") && has(t, "Client d'essai Beta") && has(t, "Poste essai A") && has(t, "API Studio 1.35.0"), "client, PC, logiciel");
        auto* table = find<ui::TableView>(*page0, "demandes.table");
        // 1.37.0 : Date, Client, Edition voulue, Etat, PC, Logiciel, Licence demandee, Message.
        check(table && rowOf(*table, 6, shared + " (ajouter ce PC)") >= 0 && rowOf(*table, 6, "nouvelle") >= 0, "la licence demandee (une existante le dit)");
        check(table && rowOf(*table, 6, shared + " (ajouter ce PC) \xE2\x80\x94 \xC3\xA0 v\xC3\xA9rifier") >= 0 && rowOf(*table, 3, "\xC3\xA0 v\xC3\xA9rifier") >= 0 &&
                  rowOf(*table, 3, "\xC3\xA0 v\xC3\xA9rifier") == rowOf(*table, 1, "Client d'essai Beta"),
              "la demande d'ajout a une licence existante : << a verifier >> (licence et etat)");
        check(selectRow(*page0, 1, "Client d'essai Beta"), "(la demande de B choisie)");
        const std::string tb = paintPage(*page0, 1600.f, 760.f);
        check(has(tb, "\xC3\x80 v\xC3\xA9rifier") && has(tb, shared) && has(tb, "confirme avec ton client"), "choisie : la page le dit");
        check(selectRow(*page0, 1, "Client d'essai Gamma") && !has(paintPage(*page0, 1600.f, 760.f), "confirme avec ton client"),
              "une demande de licence neuve : rien a verifier");
        check(has(t, "En attente") && has(t, "Message d'essai de A") && has(t, "2 en attente"), "l'etat, le message, le compte");
        auto* filter = find<ui::DropDown>(*page0, "demandes.filtre");
        filter->setSelectedIndex(1);
        const std::string ta = paintPage(*page0, 1600.f, 760.f);
        check(rows(*page0, "demandes.table") == 3 && has(ta, "Illisible"), "Toutes : l'illisible aussi, et pourquoi");
        filter->setSelectedIndex(0);
        auto dash = gui::makeDashboardPage(h0);
        const std::string dt = paintPage(*dash);
        check(f0.ws->dashboard().onlinePending == 2 && has(dt, "dont 2 en ligne"), "le tableau de bord : le compte");
    }

    std::printf("-- accepter, accepter en ligne, une licence existante, un travail en cours (sans signer)\n");
    {
        check(selectRow(*page0, 1, "Client d'essai Gamma"), "la demande de A choisie");
        std::size_t dialogs = rig0.dialogs.size();
        check(click(*page0, "demandes.accepter") && rig0.dialogs.size() == dialogs + 1, "Accepter... ouvre l'assistant");
        {
            auto& d = *rig0.dialogs.back();
            ui::Widget& w = *d.widgetRoot();
            const std::string t = paintDialog(d);
            check(has(t, "Nouvelle licence") && rows(w, "wizard.demandes") == 1 && has(t, "Poste essai A"), "l'assistant : la demande de A");
            check(find<ui::RadioButton>(w, "wizard.mode.lies")->selected(), "une licence liee a ce PC");
            check(walkToSign(d, true) && find<ui::Button>(w, "wizard.signer")->text() == "Signer et publier", "<< Signer et publier >> (rien a enregistrer)");
            check(find<ui::InputText>(w, "wizard.client") && find<ui::InputText>(w, "wizard.client")->text() == "Client d'essai Gamma", "le client prerempli");
            check(click(w, "wizard.annuler"), "Annuler");
        }
        dialogs = rig0.dialogs.size();
        check(click(*page0, "demandes.accepter-en-ligne") && rig0.dialogs.size() == dialogs + 1, "Accepter en ligne... ouvre l'assistant");
        {
            auto& d = *rig0.dialogs.back();
            ui::Widget& w = *d.widgetRoot();
            const std::string t = paintDialog(d);
            check(find<ui::RadioButton>(w, "wizard.mode.en-ligne")->selected() && shown(w, "wizard.bail") && rows(w, "wizard.demandes") == 0,
                  "une licence en ligne : aucun PC lie, le bail");
            check(has(t, "En r\xC3\xA9ponse \xC3\xA0 la demande n\xC2\xB0 1"), "elle repond a la demande n 1");
            check(walkToSign(d, true) && find<ui::Button>(w, "wizard.signer")->text() == "Signer et d\xC3\xA9poser", "<< Signer et deposer >>");
            check(click(w, "wizard.annuler"), "Annuler");
        }
        check(selectRow(*page0, 1, "Client d'essai Beta"), "la demande de B choisie");
        check(click(*page0, "demandes.accepter") && !rig0.flashes.empty() && has(rig0.flashes.back(), "confirme avec ton client"), "Accepter : l'avertissement");
        {
            auto& d = *rig0.dialogs.back();
            const std::string t = paintDialog(d);
            check(has(t, "Ajouter des PC \xC3\xA0 la licence " + shared) && rows(*d.widgetRoot(), "wizard.pc.existants") == 1, "l'assistant en ajout de PC");
            check(click(*d.widgetRoot(), "wizard.annuler"), "Annuler");
        }
        startBusy(rig0);
        page0->refresh();
        check(!find<ui::Button>(*page0, "demandes.accepter")->enabled() && !find<ui::Button>(*page0, "demandes.relever")->enabled() &&
                  !find<ui::Button>(*page0, "demandes.refuser")->enabled(),
              "un travail en cours : les boutons attendent");
        endBusy(rig0);
        page0->refresh();
        check(find<ui::Button>(*page0, "demandes.accepter")->enabled(), "puis ils reviennent");
    }

    // ================================================================================== le vrai service local
    std::printf("-- le vrai service local : les PC demandent, Relever\n");
    Fixture fx(source, "demandes");
    check(fx.ready(), "un second poste d'essai (cle d'essai, registre)");
    const lic::SigningKey* editorKey = fx.ws->signingKey();
    if (!editorKey) return finish();
    const lic::SigningKey& editor = *editorKey;
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({editor.publicKeyHex()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    check(svc.ready(), "le service demarre (" + svc.error() + ")");
    if (!svc.ready()) return finish();
    const std::string url = svc.url();
    check(fx.licence(shared, "Client d'essai Beta", "PROFESSIONAL", {fx.request("pc-x", "Poste essai X")}).ok, "la licence existante de Beta (par fichier)");
    HostRig rig(fx);
    auto h = rig.host();
    {
        check(fx.ws->serviceConfigure(url).ok && fx.ws->service().ready(), "le service local regle");
        std::string keyHex;
        check(fx.ws->serviceReadKey(keyHex).ok && fx.ws->serviceCertify(keyHex).ok, "la cle du service certifiee");
        gui::ServiceJob reg;
        reg.kind = gui::ServiceJob::Kind::Registry;
        const auto r = fx.ws->serviceRun(reg);
        check(r.ok, "le registre depose (" + r.text() + ")");
    }
    auto page = gui::makeRequestsPage(h);
    rig.refreshers.push_back([&] { page->refresh(); });
    Pc pcA(fx.root / "pc-a", "Poste essai A"), pcB(fx.root / "pc-b", "Poste essai B"), pcC(fx.root / "pc-c", "Poste essai C"),
        pcD(fx.root / "pc-d", "Poste essai D"), pcE(fx.root / "pc-e", "Poste essai E");
    {
        check(post(url, pcA, editor, {}, "Client d'essai Gamma") && post(url, pcB, editor, shared, "Client d'essai Beta") &&
                  post(url, pcC, editor, {}, "Client d'essai Delta") && post(url, pcD, editor, {}, "Client d'essai Epsilon"),
              "quatre PC deposent leur demande (201)");
        page->refresh();
        check(shown(*page, "demandes.relever") && has(paintPage(*page), "jamais relev\xC3\xA9" "es"), "jamais relevees");
        check(click(*page, "demandes.relever") && rig.last() && rig.last()->ok, "Relever : " + lastText(rig));
        const std::string t = paintPage(*page, 2000.f, 760.f);
        check(rows(*page, "demandes.table") == 4 && has(t, "4 en attente") && has(t, "relev\xC3\xA9" "es le "), "quatre en attente, la date du releve");
        check(has(t, "Client d'essai Epsilon") && has(t, "Message d'essai de Poste essai D"), "le client, le message (ouverts avec la cle de l'editeur)");
        check(has(t, "\xC3\x89" "dition voulue"), "1.37.0 : la colonne de l'edition voulue (la boutique du Studio)");
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(!posteState(url, pcA, editor, st, sc), "rien n'est encore publie pour A");
    }

    std::printf("-- accepter : l'assistant, << Signer et publier >>, le PC lit sa licence\n");
    {
        check(selectRow(*page, 1, "Client d'essai Gamma"), "la demande de A choisie");
        check(click(*page, "demandes.accepter"), "Accepter...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        check(walkToSign(d, true), "jusqu'a l'etape 6");
        std::string t = paintDialog(d);
        checkText(!shown(w, "wizard.confirmer-ajout") && has(t, "Aucun fichier \xC3\xA0 envoyer"), "une licence neuve : rien a envoyer, pas de case", t);
        const std::size_t asked = rig.asked.size();
        g_now += 60;
        check(click(w, "wizard.signer"), "Signer et publier");
        check(rig.asked.size() == asked, "aucun fichier demande");
        check(rig.last() && rig.last()->ok, "publiee : " + lastText(rig));
        t = paintDialog(d);
        checkText(has(t, "publi\xC3\xA9" "e sur le service") && has(t, "Rien d'autre \xC3\xA0 faire"), "l'assistant le dit", t);
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(posteState(url, pcA, editor, st, sc) && st.state == gw::State::Accepted && st.requestNonce == pcA.nonce && !licenceIdOf(sc.license).empty() &&
                  st.licenseId.empty(),
              "A lit << acceptee >> et SA licence (scellee pour lui, son identifiant pas en clair)");
        check(!fx.ws->hasServiceJob(), "plus rien a publier");
        check(click(w, "wizard.fermer"), "Fermer");
        fx.ws->reload();
        page->refresh();
        check(rows(*page, "demandes.table") == 3, "A n'est plus en attente");
    }

    std::printf("-- une licence existante : l'ajout du PC (a verifier, la case a cocher)\n");
    {
        check(selectRow(*page, 1, "Client d'essai Beta"), "la demande de B choisie");
        check(click(*page, "demandes.accepter"), "Accepter...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Ajouter des PC \xC3\xA0 la licence " + shared) && rows(w, "wizard.pc.existants") == 1, "en ajout de PC a " + shared);
        auto* reqs = find<ui::TableView>(w, "wizard.demandes");
        check(reqs && rowOf(*reqs, 1, "\xC3\xA0 v\xC3\xA9rifier") == 0, "l'assistant : la demande de B << a verifier >>");
        check(walkToSign(d, false), "jusqu'a l'etape 6");
        t = paintDialog(d);
        check(shown(w, "wizard.confirmer-ajout") && has(t, "J'ai v\xC3\xA9rifi\xC3\xA9 avec mon client"), "l'avertissement et la case a cocher");
        g_now += 60;
        const std::size_t devices = fx.ws->licence(shared)->licence.devices.size();
        check(click(w, "wizard.signer"), "Signer et publier (sans cocher)");
        t = paintDialog(d);
        check(fx.ws->licence(shared)->licence.devices.size() == devices && !fx.ws->hasServiceJob() && has(t, "Coche d'abord"), "pas cochee : rien n'est signe, et c'est dit");
        auto* box = find<ui::Checkbox>(w, "wizard.confirmer-ajout");
        if (box) box->setState(ui::Checkbox::State::Checked);
        check(click(w, "wizard.signer") && rig.last() && rig.last()->ok, "Signer et publier : " + lastText(rig));
        check(fx.ws->licence(shared) && fx.ws->licence(shared)->licence.devices.size() == 2, "deux PC sur la licence");
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(posteState(url, pcB, editor, st, sc) && st.state == gw::State::Accepted && st.licenseId.empty() && licenceIdOf(sc.license) == shared,
              "B << acceptee >> : la licence " + shared + " scellee pour lui, son identifiant pas en clair");
        (void)click(w, "wizard.fermer");
    }

    std::printf("-- accepter en ligne : le code montre (dans le dialogue), garde chiffre, scelle pour le PC de la demande\n");
    std::string code;
    {
        fx.ws->reload();
        page->refresh();
        check(selectRow(*page, 1, "Client d'essai Epsilon"), "la demande de D choisie");
        check(click(*page, "demandes.accepter-en-ligne"), "Accepter en ligne...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "En r\xC3\xA9ponse \xC3\xA0 la demande n\xC2\xB0 " + std::to_string(pcD.number)), "elle repond a la demande de D");
        check(walkToSign(d, true), "jusqu'a l'etape 6");
        check(find<ui::InputText>(w, "wizard.identifiant") != nullptr, "(l'identifiant propose)");
        const std::string id = find<ui::InputText>(w, "wizard.identifiant")->text();
        const std::size_t dialogs = rig.dialogs.size();
        g_now += 60;
        check(click(w, "wizard.signer") && rig.last() && rig.last()->ok, "Signer et deposer : " + lastText(rig));
        check(rig.dialogs.size() == dialogs + 1 && rig.dialogs.back()->widgetRoot() && rig.dialogs.back()->widgetRoot()->findById("code.copier"),
              "le depot reussi : le dialogue du code s'ouvre");
        const gui::LicenceEntry* l = fx.ws->licence(id);
        check(l && l->licence.service == url && l->licence.devices.empty(), "la licence en ligne : le service, aucun PC lie");
        if (rig.dialogs.size() == dialogs + 1) {
            auto& cd = *rig.dialogs.back();
            g_copied.clear();
            check(has(paintDialog(cd), "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-\xE2\x80\xA2"), "le code masque d'abord");
            check(click(*cd.widgetRoot(), "code.copier") && g_copied.size() == 23 && g_copied.rfind("XPG-", 0) == 0, "Copier : le code (XPG-XXXX-XXXX-XXXX-XXXX)");
            code = g_copied;
            g_copied.clear();
            check(!has(paintDialog(cd), code), "toujours masque a l'ecran");
            check(click(*cd.widgetRoot(), "code.fermer"), "J'ai note le code");
        }
        t = paintDialog(d);
        check(has(t, "La licence est sur le service"), "l'assistant le dit");
        std::string kept, keptWhy;
        check(!code.empty() && online::readCode(fx.admin.string(), id, kept, &keptWhy) && kept == code, "1.37.0 : le code garde sur ce poste (Voir le code...) " + keptWhy);
        check(!code.empty() && !foundOutside(fx.root, code, "codes-activation"), "le code n'est ecrit dans aucun autre fichier du poste");
        bool inReports = false;
        for (const auto& r : rig.reports) inReports = inReports || has(r.text(), code);
        for (const auto& f : rig.flashes) inReports = inReports || has(f, code);
        check(!code.empty() && !inReports, "ni dans un rapport, ni dans un message");
        (void)click(w, "wizard.fermer");

        json::Value b = json::Value::object();
        b.set("code", code);
        b.set("demande", pcE.request({}, lic::systemNow()));
        b.set("nom", pcE.name);
        const Reply a = call(url, "POST", "/v1/activer", json::write(b, false), pcE.key.get());
        check(a.status == 200 && a.value.isObject() && !a.value.str("activation").empty(), "un PC s'active avec ce code (" + a.body.substr(0, 160) + ")");
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(posteState(url, pcD, editor, st, sc) && st.state == gw::State::Accepted && sc.license.empty() && sc.code == code && sc.onlineLicense == id &&
                  !has(sc.message, code),
              "1.37.0 : D lit << acceptee >> et le code scelle pour lui (son Studio s'active seul), jamais le code dans le mot");

        std::printf("-- 1.40.0 : le regard en direct - le PC libere par son client, vu seul\n");
        {
            gui::Shell shell(fx.admin.string(), [] { return g_now; });
            check(shell.autoWatch() && shell.finishSlow() && shell.live(), "le premier regard (GET /admin/etat) : en direct");
            check(has(shell.syncStatusText(), "en direct"), "le haut de la fen\xC3\xAAtre le dit : " + shell.syncStatusText());
            check(shell.autoRefreshLicences() && shell.finishSlow(), "le premier releve (rien a comparer : rien a dire)");
            const online::OnlineLicence* ol = shell.workspace().service().snapshot.licence(id);
            const std::string pcKey = pcE.key ? pcE.key->publicKeyHex() : std::string();
            check(ol && ol->activation(pcKey) && ol->activation(pcKey)->state == "active", "le PC de E est actif");
            const std::size_t logged = shell.sessionLog().size();
            check(shell.autoWatch() && shell.finishSlow(), "un regard sans changement");
            check(shell.sessionLog().size() == logged, "rien ne bouge : rien au journal");
            json::Value lb = json::Value::object();
            lb.set("licence", id);
            lb.set("station", lic::stationHash("machine d'essai " + pcE.name, pcE.key->publicKey()));
            const Reply rl = call(url, "POST", "/v1/liberer", json::write(lb, false), pcE.key.get());
            check(rl.status == 200, "E libere son PC (" + rl.body.substr(0, 120) + ")");
            // le regard voit << activite >> bouger : la coquille relit les licences des que rien ne tourne (Update)
            check(shell.autoWatch() && shell.finishSlow(), "le regard suivant");
            check(shell.autoRefreshLicences() && shell.finishSlow(), "les licences relues");
            ol = shell.workspace().service().snapshot.licence(id);
            check(ol && ol->activation(pcKey) && ol->activation(pcKey)->state == "liberee", "le releve le montre libere");
            const gui::Shell::LogLine* m = shell.lastMessage();
            check(m && has(m->result.message, "a lib\xC3\xA9r\xC3\xA9 le PC \xC2\xAB " + pcE.name + " \xC2\xBB") && has(m->result.message, id) &&
                      has(m->result.message, "sa place est libre"),
                  "un avis le dit : " + (m ? m->result.message : std::string("(rien)")));
            // le regard n'occupe pas : un travail lance pendant qu'il tourne attend sa fin, puis part seul
            check(shell.autoWatch() && !shell.busy(), "pendant le regard : pas occupe pour toi");
            bool ran = false;
            check(shell.runSlow("Essai : un travail", [] { return gui::OpResult::done("fait"); }, [&ran](const gui::OpResult&) { ran = true; }) && shell.busy(),
                  "un travail lance pendant le regard : il attend (occupe)");
            check(shell.finishSlow() && ran && !shell.busy(), "puis il part seul, et finit");
        }
    }

    std::printf("-- refuser : la raison, scellee pour le PC\n");
    {
        fx.ws->reload();
        page->refresh();
        check(selectRow(*page, 1, "Client d'essai Delta"), "la demande de C choisie");
        check(click(*page, "demandes.refuser"), "Refuser...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Refuser la demande") && has(t, "Client d'essai Delta") && has(t, "scell\xC3\xA9" "e"), "la raison est demandee, et ou elle va");
        check(click(w, "raison.ok"), "Refuser sans raison");
        t = paintDialog(d);
        check(has(t, "Donne une raison") && w.visible(), "sans raison : le dialogue reste, et le dit");
        check(type(w, "raison.texte", "Pas de contrat pour ce poste (essai)") && click(w, "raison.ok"), "Refuser avec une raison");
        check(rig.last() && rig.last()->ok, "refusee : " + lastText(rig));
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(posteState(url, pcC, editor, st, sc) && st.state == gw::State::Refused && sc.message == "Pas de contrat pour ce poste (essai)",
              "C lit << refusee >> et la raison (scellee pour lui)");
        page->refresh();
        t = paintPage(*page, 1600.f, 760.f);
        check(rows(*page, "demandes.table") == 0 && has(t, "Aucune demande n'attend"), "plus rien en attente : dit");
        auto* filter = find<ui::DropDown>(*page, "demandes.filtre");
        filter->setSelectedIndex(1);
        t = paintPage(*page, 1600.f, 760.f);
        check(rows(*page, "demandes.table") == 4 && has(t, "Accept\xC3\xA9" "e") && has(t, "Refus\xC3\xA9" "e"), "Toutes : acceptees et refusees");
        check(selectRow(*page, 1, "Client d'essai Gamma") && !find<ui::Button>(*page, "demandes.accepter")->enabled(), "une demande traitee : pas d'Accepter");
        filter->setSelectedIndex(0);
    }

    std::printf("-- une revocation publiee (publishToService) : la liste scellee pour le PC\n");
    {
        g_now += 60;
        fx.ws->reload();
        check(fx.ws->revoke(shared, "", "Fin de contrat (essai)", false).ok && fx.ws->hasServiceJob(), "revoquee : une publication attend");
        startBusy(rig);
        check(!gui::publishToService(h) && fx.ws->hasServiceJob(), "un travail en cours : elle attend (rien de perdu)");
        endBusy(rig);
        check(gui::publishToService(h) && rig.last() && rig.last()->ok, "puis elle part : " + lastText(rig));
        gw::DeviceStatus st;
        gw::StatusContent sc;
        check(posteState(url, pcB, editor, st, sc) && st.state == gw::State::Revoked && !sc.revocations.empty() &&
                  sc.revocations == readFile(fx.admin / "archive" / "revocations" / "revocations.xpgrev"),
              "B << revoquee >>, la liste de revocation scellee pour lui");
        const Reply pub = call(url, "GET", "/v1/revocations", {}, nullptr);
        check(!has(pub.body, "Fin de contrat"), "la raison n'est pas publique");
        check(!gui::publishToService(h), "plus rien a publier");
    }

    std::printf("-- hors ligne : dit, jamais << relevees >>\n");
    {
        check(fx.ws->serviceConfigure("http://127.0.0.1:9").ok, "un service d'essai ou rien n'ecoute");
        page->refresh();
        check(click(*page, "demandes.relever") && rig.last() && !rig.last()->ok && has(rig.last()->message, "pas de r\xC3\xA9ponse du service"),
              "Relever hors ligne : refuse, et dit (" + lastText(rig) + ")");
    }

    std::printf("-- le kit des Studios (Parametres de securite)\n");
    {
        auto sec = gui::makeSecurityPage(h);
        const std::string t = paintPage(*sec, 1280.f, 4000.f);
        check(has(t, "Les fichiers pour les Studios") && shown(*sec, "securite.kit-studios"), "la carte et son bouton");
        rig.picks = {U(fx.root / "kit")};
        check(click(*sec, "securite.kit-studios") && rig.last() && !rig.last()->ok && has(rig.last()->message, "ESSAI"), "une cle d'essai : refusee");
        check(!fs::exists(fx.root / "kit" / "PublisherKey.hpp"), "rien d'ecrit");
    }

    return finish();
}
