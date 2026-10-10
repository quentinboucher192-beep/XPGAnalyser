#include "HmiExamples.hpp"
#include "HmiNavigation.hpp"
#include "HmiSymbols.hpp"

#include "HmiAssets.hpp"
#include "HmiCrypto.hpp"
#include "HmiMedia.hpp"
#include "HmiForms.hpp"
#include "HmiGuide.hpp"
#include "HmiRuntime.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace hmi::examples {

// ---- lot 16 : un GIF anime ecrit ici (un ventilateur qui tourne) ------------------------
//  Quatre pales (l'une jaune : on voit les tours), un moyeu, une couronne, sur le
//  fond des exemples ; `frames` images pour un tour, lissees (4 x 4 points par
//  pixel, la palette porte les nuances).
Bytes fanGif(int size, int frames, int delayMs) {
    size = std::clamp(size, 16, 512);
    frames = std::clamp(frames, 2, 120);
    // La palette : le fond, puis 4 nuances de chaque couleur vers le fond.
    const std::uint32_t back = 0x1B2029;
    const std::uint32_t inks[] = {0x4FA3FF, 0xF2C94C, 0xC8D0DC, 0x5A6678};   // pale, pale marquee, moyeu, couronne
    std::vector<std::uint32_t> palette{back};
    const auto mix = [](std::uint32_t a, std::uint32_t b, double t) {
        const auto ch = [&](int shift) {
            const double va = (a >> shift) & 0xFF, vb = (b >> shift) & 0xFF;
            return static_cast<std::uint32_t>(std::lround(va + (vb - va) * t)) << shift;
        };
        return ch(16) | ch(8) | ch(0);
    };
    for (const auto ink : inks)
        for (int level = 1; level <= 4; ++level) palette.push_back(mix(back, ink, level / 4.0));
    std::vector<GifImage> images;
    const double c = size / 2.0;
    const double pi = 3.14159265358979323846;
    for (int f = 0; f < frames; ++f) {
        GifImage img;
        img.delayMs = delayMs;
        img.indices.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0);
        const double turn = 2 * pi * f / frames;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                int hits[4] = {0, 0, 0, 0};
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const double px = x + (sx + 0.5) / 4.0 - c, py = y + (sy + 0.5) / 4.0 - c;
                        const double r = std::hypot(px, py);
                        if (r < size * 0.085) {
                            ++hits[2];
                            continue;
                        }
                        bool blade = false;
                        for (int k = 0; k < 4 && !blade; ++k) {
                            const double a = turn + k * pi / 2;
                            // Dans le repere de la pale : u le long, v en travers.
                            const double u = px * std::cos(a) + py * std::sin(a) - size * 0.23;
                            const double v = -px * std::sin(a) + py * std::cos(a) - (u * 0.18);
                            if ((u * u) / std::pow(size * 0.19, 2) + (v * v) / std::pow(size * 0.075, 2) <= 1.0) {
                                ++hits[k == 0 ? 1 : 0];
                                blade = true;
                            }
                        }
                        if (blade) continue;
                        if (r > size * 0.445 && r < size * 0.485) ++hits[3];
                    }
                int best = -1, count = 0;
                for (int k = 0; k < 4; ++k)
                    if (hits[k] > count) {
                        best = k;
                        count = hits[k];
                    }
                if (best < 0) continue;
                const int level = std::clamp((count * 4 + 8) / 16, 1, 4);
                img.indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                    static_cast<std::uint8_t>(1 + best * 4 + (level - 1));
            }
        images.push_back(std::move(img));
    }
    return encodeGif(size, size, palette, images, 0);
}

namespace {

// ---- les images des exemples : des SVG ecrits ici (aucun fichier a livrer) -----
std::string valveSvg(bool open) {
    const char* body = open ? "#2ECC71" : "#5A6474";
    return std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"128\" height=\"128\" viewBox=\"0 0 128 128\">")
         + "<rect x=\"4\" y=\"56\" width=\"120\" height=\"16\" fill=\"#3A4556\"/>"
         + "<polygon points=\"16,30 64,64 16,98\" fill=\"" + body + "\" stroke=\"#0F1318\" stroke-width=\"3\"/>"
         + "<polygon points=\"112,30 64,64 112,98\" fill=\"" + body + "\" stroke=\"#0F1318\" stroke-width=\"3\"/>"
         + "<rect x=\"60\" y=\"14\" width=\"8\" height=\"50\" fill=\"#9AA6B8\"/>"
         + "<rect x=\"40\" y=\"6\" width=\"48\" height=\"12\" rx=\"3\" fill=\"" + body + "\"/></svg>";
}

// Une pompe : sa volute, sa roue tournee de `angle` degres, sa couleur.
std::string pumpSvg(int angle, const char* color) {
    return std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"128\" height=\"128\" viewBox=\"0 0 128 128\">")
         + "<rect x=\"78\" y=\"20\" width=\"46\" height=\"22\" fill=\"#3A4556\"/>"
         + "<circle cx=\"64\" cy=\"68\" r=\"50\" fill=\"" + color + "\" stroke=\"#0F1318\" stroke-width=\"4\"/>"
         + "<g transform=\"rotate(" + std::to_string(angle) + " 64 68)\">"
         + "<polygon points=\"64,68 58,26 70,26\" fill=\"#F4F6FA\"/>"
         + "<polygon points=\"64,68 58,26 70,26\" fill=\"#F4F6FA\" transform=\"rotate(120 64 68)\"/>"
         + "<polygon points=\"64,68 58,26 70,26\" fill=\"#F4F6FA\" transform=\"rotate(240 64 68)\"/></g>"
         + "<circle cx=\"64\" cy=\"68\" r=\"9\" fill=\"#0F1318\"/></svg>";
}

std::string posterSvg() {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"320\" height=\"180\" viewBox=\"0 0 320 180\">"
           "<rect width=\"320\" height=\"180\" fill=\"#16324F\"/>"
           "<rect x=\"30\" y=\"40\" width=\"70\" height=\"110\" rx=\"8\" fill=\"#3A6EA5\"/>"
           "<rect x=\"125\" y=\"40\" width=\"70\" height=\"110\" rx=\"8\" fill=\"#3A6EA5\"/>"
           "<rect x=\"220\" y=\"40\" width=\"70\" height=\"110\" rx=\"8\" fill=\"#2ECC71\"/>"
           "<rect x=\"20\" y=\"150\" width=\"280\" height=\"8\" fill=\"#9AA6B8\"/></svg>";
}

// ---- de quoi ecrire un exemple en peu de lignes --------------------------------------
class Maker {
public:
    explicit Maker(int height = kHeight) {
        auto& p = ex_.project;
        p.security = Security::defaults();
        p.security.enabled = false;
        p.config.name = "Exemple";
        p.config.width = kWidth;
        p.config.height = height;
        p.config.cycleMs = 50;
        View v = makeView(p, "Exemple");
        v.width = kWidth;
        v.height = height;
        v.background = "#1B2029";
        p.config.startView = v.id;
        p.views.push_back(std::move(v));
    }
    Project& project() { return ex_.project; }
    View& view() { return ex_.project.views.front(); }

    Object& add(Kind k, const std::string& name, double x, double y, double w = 0, double h = 0) {
        auto& v = view();
        Object o = makeObject(k, ex_.project.allocate(), name, x, y, v.activeLayer);
        if (w > 0) o.setNumber("w", w);
        if (h > 0) o.setNumber("h", h);
        v.objects.push_back(std::move(o));
        return v.objects.back();
    }
    // Un texte d'explication, discret.
    Object& note(const std::string& name, double x, double y, double w, double h, const std::string& text) {
        auto& o = add(Kind::Text, name, x, y, w, h);
        o.set("text", text);
        o.setNumber("fontSize", 14);
        o.set("textColor", "#9AA6B8");
        o.setFlag("wrap", true);
        return o;
    }
    Object& title(const std::string& name, double x, double y, double w, const std::string& text, double size = 20) {
        auto& o = add(Kind::Text, name, x, y, w, size * 1.8);
        o.set("text", text);
        o.setNumber("fontSize", size);
        o.set("textColor", "#E6EAF0");
        return o;
    }
    void var(const std::string& name, const std::string& type, const std::string& initial) {
        Variable v;
        v.id = ex_.project.allocate();
        v.name = name;
        v.type = type;
        v.initial = initial;
        ex_.project.programs.variables.push_back(std::move(v));
    }
    // Le script qui fait vivre l'exemple : toutes les 50 ms ; T compte les secondes.
    void cyclic(const std::string& body) {
        if (!ex_.project.variable("T")) var("T", "REAL", "0.0");
        Script s;
        s.id = ex_.project.allocate();
        s.name = "Anime";
        s.event = "Cyclique";
        s.periodMs = 50;
        s.body = "T := T + 0.05;\n" + body;
        ex_.project.programs.scripts.push_back(std::move(s));
    }
    void svg(const std::string& name, const std::string& text) {
        auto blob = std::make_shared<Bytes>(text.begin(), text.end());
        ex_.project.assets.resources.push_back(makeResource(ex_.project, name, blob, "exemple"));
    }
    User& user(const std::string& login, const std::string& fullName, Id group, const std::string& password) {
        User u;
        u.id = ex_.project.allocate();
        u.login = login;
        u.fullName = fullName;
        u.group = group;
        if (!password.empty()) {
            u.salt = randomHex(16);
            u.passwordHash = passwordHash(u.salt, password);
        }
        ex_.project.security.users.push_back(std::move(u));
        return ex_.project.security.users.back();
    }
    void secure(const std::string& startUser) {
        ex_.project.security.enabled = true;
        ex_.project.security.startUser = startUser;
    }
    // Lot 12 : une autre vue du projet (navigation), a la taille de l'exemple ; ses
    // objets se posent par addIn.
    Id extraView(const std::string& name, Id up = kNoId) {
        View v = makeView(ex_.project, name);
        v.width = kWidth;
        v.height = ex_.project.config.height;
        v.background = "#1B2029";
        v.upView = up;
        const Id id = v.id;
        ex_.project.views.push_back(std::move(v));
        return id;
    }
    Object& addIn(Id viewId, Kind k, const std::string& name, double x, double y, double w = 0, double h = 0) {
        View& v = *ex_.project.view(viewId);
        Object o = makeObject(k, ex_.project.allocate(), name, x, y, v.activeLayer);
        if (w > 0) o.setNumber("w", w);
        if (h > 0) o.setNumber("h", h);
        v.objects.push_back(std::move(o));
        return v.objects.back();
    }
    void step(double at, std::string op, std::string object = {}, std::string arg = {}) {
        ex_.steps.push_back({at, std::move(op), std::move(object), std::move(arg)});
    }
    Example done(double period) {
        ex_.period = period;
        return std::move(ex_);
    }

private:
    Example ex_;
};

constexpr Id kOperateur = 0xFFFFFE01u, kSuperviseur = 0xFFFFFE03u, kAdministrateur = 0xFFFFFE04u;

Action click(Operation op, std::string target, std::string value = {}) {
    Action a;
    a.trigger = Trigger::Click;
    a.operation = op;
    a.target = std::move(target);
    a.value = std::move(value);
    return a;
}

// ---- les exemples -------------------------------------------------------------------
Example text() {
    Maker m;
    m.var("Temperature", "REAL", "21.0");
    m.cyclic("Temperature := 21.0 + 3.0 * SIN(T * 0.8);");
    auto& t = m.title("Titre_Temperature", 30, 30, 540, "Temp\xC3\xA9rature du local : {Temperature:0.0} \xC2\xB0" "C", 22);
    t.setExpr("textColor", "SEL(Temperature > 23.0, '#E6EAF0', '#FF6B6B')");
    m.note("Explication", 30, 110, 540, 150,
           "Un texte est fixe, ou \xC3\xA0 trous : {Temperature:0.00} \xC2\xB0" "C est remis \xC3\xA0 jour \xC3\xA0 chaque cycle. Au-dessus de 23 \xC2\xB0" "C, "
           "une expression passe le titre en rouge. Ce paragraphe revient \xC3\xA0 la ligne tout seul au bord de l'objet.");
    return m.done(12);
}

Example image() {
    Maker m;
    m.svg("vanne_ouverte.svg", valveSvg(true));
    m.svg("vanne_fermee.svg", valveSvg(false));
    m.var("Ouverte", "BOOL", "FALSE");
    m.cyclic("Ouverte := (REAL_TO_INT(T) MOD 4) < 2;");
    auto& img = m.add(Kind::Image, "Vanne_V1", 50, 40, 200, 200);
    img.set("image", "vanne_fermee.svg");
    img.setExpr("image", "SEL(Ouverte, 'vanne_fermee.svg', 'vanne_ouverte.svg')");
    m.title("Etat_Vanne", 290, 70, 290, "Vanne V1 : {Ouverte:ouverte|ferm\xC3\xA9" "e}");
    m.note("Explication", 290, 130, 290, 120,
           "L'image vient des ressources du projet. Une expression choisit laquelle montrer : "
           "SEL(Ouverte, 'vanne_fermee.svg', 'vanne_ouverte.svg').");
    return m.done(8);
}

Example button() {
    Maker m;
    m.var("Marche", "BOOL", "FALSE");
    auto& b = m.add(Kind::Button, "Btn_Marche", 50, 90, 220, 60);
    b.set("text", "{Marche:Arr\xC3\xAAter|D\xC3\xA9marrer}");
    b.setNumber("fontSize", 18);
    b.setExpr("fill", "SEL(Marche, '#2F6FD6', '#8A3A3A')");
    b.actions.push_back(click(Operation::Toggle, "Marche"));
    auto& v = m.add(Kind::Indicator, "Voyant_Marche", 320, 100, 40, 40);
    v.setExpr("value", "Marche");
    m.title("Etat_Moteur", 380, 102, 210, "Moteur {Marche:en marche|arr\xC3\xAAt\xC3\xA9}", 18);
    // Lot 9 : une commande sensible, a confirmer par un second clic.
    auto& stop = m.add(Kind::Button, "Btn_Arret", 50, 175, 220, 50);
    stop.set("text", "Arr\xC3\xAAt");
    stop.set("fill", "#8A3A3A");
    stop.set("confirmMode", "second clic");
    stop.actions.push_back(click(Operation::Reset, "Marche"));
    m.note("Explication", 300, 160, 290, 130,
           "Le bouton porte une action (onglet Actions) : Clic \xE2\x86\x92 Basculer Marche. Son texte et sa couleur suivent "
           "la variable. Arr\xC3\xAAt demande une confirmation : un second clic dans les 3 s.");
    m.step(1.0, "clic", "Btn_Marche");
    m.step(2.6, "clic", "Btn_Arret");
    m.step(3.4, "clic", "Btn_Arret");
    m.step(6.0, "clic", "Btn_Marche");
    m.step(8.5, "clic", "Btn_Marche");
    return m.done(10);
}

Example rectangle() {
    Maker m;
    m.var("Niveau", "REAL", "50.0");
    m.cyclic("Niveau := 50.0 + 45.0 * SIN(T * 0.6);");
    auto& frame = m.add(Kind::Rectangle, "Cadre", 40, 70, 520, 60);
    frame.set("fill", "#141820");
    frame.setNumber("radius", 6);
    auto& bar = m.add(Kind::Rectangle, "Niveau_Cuve", 44, 74, 256, 52);
    bar.set("stroke", "");
    bar.setNumber("strokeWidth", 0);
    bar.setExpr("w", "Niveau * 5.12");
    bar.setExpr("fill", "SEL(Niveau > 80.0, SEL(Niveau > 60.0, '#2F6FD6', '#F2994A'), '#E5534B')");
    m.title("Valeur_Niveau", 40, 150, 520, "Niveau : {Niveau:0} %", 18);
    m.note("Explication", 40, 200, 520, 80,
           "La largeur et la couleur du rectangle sont pilot\xC3\xA9" "es par des expressions : bleu, orange au-dessus de 60 %, "
           "rouge au-dessus de 80 %.");
    return m.done(12);
}

Example ellipse() {
    Maker m;
    m.var("Alarme", "BOOL", "FALSE");
    m.cyclic("Alarme := (REAL_TO_INT(T) MOD 6) >= 3;");
    auto& e = m.add(Kind::Ellipse, "Lampe_Alarme", 70, 50, 160, 160);
    e.setExpr("fill", "SEL(Alarme, '#3A4556', '#E5534B')");
    e.setExpr("blink", "Alarme");
    m.title("Etat_Alarme", 280, 90, 300, "Alarme : {Alarme:ACTIVE|au repos}");
    m.note("Explication", 280, 150, 300, 100,
           "Le remplissage et le clignotement de l'ellipse suivent la m\xC3\xAAme variable.");
    return m.done(12);
}

Example line() {
    Maker m;
    m.var("Angle", "REAL", "0.0");
    m.cyclic("Angle := 60.0 * SIN(T * 0.9);");
    auto& dial = m.add(Kind::Ellipse, "Cadran", 180, 40, 240, 240);
    dial.set("fill", "#141820");
    dial.set("stroke", "#4A5566");
    auto& needle = m.add(Kind::Line, "Aiguille", 300, 60, 0, 100);
    needle.set("points", "0,0 0,100");
    needle.setNumber("h", 100);
    needle.set("stroke", "#F2994A");
    needle.setNumber("strokeWidth", 6);
    needle.setNumber("pivotX", 0.5);
    needle.setNumber("pivotY", 1.0);
    needle.setExpr("rot", "Angle");
    auto& hub = m.add(Kind::Ellipse, "Axe", 290, 150, 20, 20);
    hub.set("fill", "#E6EAF0");
    m.title("Valeur_Angle", 20, 30, 150, "{Angle:0}\xC2\xB0", 22);
    m.note("Explication", 440, 60, 150, 200, "La rotation de la ligne suit une variable, autour de son pivot (en bas).");
    return m.done(10);
}

Example polygon() {
    Maker m;
    m.var("X", "REAL", "40.0");
    m.cyclic("X := X + 4.0;\nIF X > 480.0 THEN X := 40.0; END_IF;");
    auto& pipe = m.add(Kind::Rectangle, "Tuyau", 20, 110, 560, 44);
    pipe.set("fill", "#2A313C");
    pipe.set("stroke", "#4A5566");
    auto& arrow = m.add(Kind::Polygon, "Fleche", 40, 114, 70, 36);
    arrow.set("points", "0,0 46,0 70,18 46,36 0,36 18,18");
    arrow.set("fill", "#4FA3FF");
    arrow.set("stroke", "");
    arrow.setExpr("x", "X");
    m.note("Explication", 20, 180, 560, 90,
           "Un polygone (ses points dans le rep\xC3\xA8re de l'objet) dont la position X suit une variable : le sens de "
           "circulation dans la tuyauterie.");
    return m.done(10);
}

Example indicator() {
    Maker m;
    m.var("Marche", "BOOL", "FALSE");
    m.var("Defaut", "BOOL", "FALSE");
    m.var("Maintenance", "BOOL", "FALSE");
    m.cyclic("Marche := (REAL_TO_INT(T) MOD 4) < 2;\nDefaut := (REAL_TO_INT(T * 2.0) MOD 7) = 0;\n"
             "Maintenance := (REAL_TO_INT(T) MOD 10) >= 7;");
    auto& a = m.add(Kind::Indicator, "Voyant_Marche", 90, 70, 56, 56);
    a.setExpr("value", "Marche");
    auto& b = m.add(Kind::Indicator, "Voyant_Defaut", 272, 70, 56, 56);
    b.setExpr("value", "Defaut");
    b.set("colorOn", "#E5534B");
    b.setExpr("blink", "Defaut");
    auto& c = m.add(Kind::Indicator, "Voyant_Maintenance", 454, 70, 56, 56);
    c.setExpr("value", "Maintenance");
    c.set("colorOn", "#F2994A");
    c.set("shape", "carr\xC3\xA9");
    auto& la = m.add(Kind::Text, "Libelle_Marche", 40, 140, 156, 30);
    la.set("text", "Marche");
    la.set("align", "centre");
    auto& lb = m.add(Kind::Text, "Libelle_Defaut", 222, 140, 156, 30);
    lb.set("text", "D\xC3\xA9" "faut");
    lb.set("align", "centre");
    auto& lc = m.add(Kind::Text, "Libelle_Maintenance", 404, 140, 156, 30);
    lc.set("text", "Maintenance");
    lc.set("align", "centre");
    m.note("Explication", 40, 200, 520, 80,
           "Un voyant allum\xC3\xA9 ou \xC3\xA9teint selon sa Valeur (une expression bool\xC3\xA9" "enne) ; le d\xC3\xA9" "faut clignote, "
           "la maintenance est carr\xC3\xA9" "e.");
    return m.done(10);
}

Example progressBar() {
    Maker m;
    m.var("Niveau", "REAL", "50.0");
    m.cyclic("Niveau := 50.0 + 45.0 * SIN(T * 0.7);");
    auto& h = m.add(Kind::ProgressBar, "Barre_Niveau", 40, 70, 380, 32);
    h.setExpr("value", "Niveau");
    auto& v = m.add(Kind::ProgressBar, "Barre_Verticale", 490, 30, 44, 240);
    v.set("orientation", "verticale");
    v.set("fill", "#2ECC71");
    v.setExpr("value", "Niveau");
    m.title("Valeur_Niveau", 40, 120, 380, "Niveau : {Niveau:0.0} %", 18);
    m.note("Explication", 40, 180, 400, 100,
           "Horizontale ou verticale, entre son minimum et son maximum (0 et 100 ici).");
    return m.done(12);
}

Example gauge() {
    Maker m;
    m.var("Pression", "REAL", "20.0");
    m.cyclic("Pression := 20.0 + 15.0 * SIN(T * 0.5);");
    auto& g = m.add(Kind::Gauge, "Jauge_Pression", 40, 40, 220, 220);
    g.setExpr("value", "Pression");
    g.setNumber("max", 40);
    g.set("unit", "bar");
    auto& d = m.add(Kind::Gauge, "Jauge_Charge", 330, 40, 220, 220);
    d.setExpr("value", "Pression * 2.5");
    d.set("unit", "%");
    d.set("fill", "#2ECC71");
    return m.done(12);
}

Example table() {
    Maker m;
    m.var("PA", "REAL", "18.0");
    m.var("PB", "REAL", "24.0");
    m.cyclic("PA := 18.0 + 4.0 * SIN(T);\nPB := 24.0 + 3.0 * COS(T * 0.7);");
    auto& t = m.add(Kind::Table, "Mesures", 30, 30, 540, 150);
    t.set("columns", "Mesure;Valeur;Unit\xC3\xA9");
    t.set("cells", formatCells({{"Pression A", "{PA:0.0}", "bar"},
                                {"Pression B", "{PB:0.0}", "bar"},
                                {"D\xC3\xA9" "bit", "{PA * 1.8:0.0}", "kg/h"},
                                {"\xC3\x89tat", "=SEL(PA > 20.0, 'normale', 'haute')", ""}}));
    t.setNumber("rows", 4);
    m.note("Explication", 30, 200, 540, 90,
           "Chaque case : un texte, un texte \xC3\xA0 trous ({{PA:0.0}}) ou =expression. Le contenu se r\xC3\xA8gle dans l'onglet "
           "Contenu ; un tableau peut aussi montrer un fichier externe.");
    return m.done(12);
}

Example history() {
    Maker m;
    m.var("Pression", "REAL", "20.0");
    m.cyclic("Pression := 21.0 + 13.0 * SIN(T * 0.6);");
    AlarmDef high;
    high.id = m.project().allocate();
    high.name = "Pression_Haute";
    high.condition = "Pression > 30.0";
    high.message = "Pression haute : {Pression:0.0} bar";
    high.priority = 2;
    high.ackRequired = false;
    m.project().alarms.push_back(high);
    AlarmDef low = high;
    low.id = m.project().allocate();
    low.name = "Pression_Basse";
    low.condition = "Pression < 12.0";
    low.message = "Pression basse : {Pression:0.0} bar";
    low.priority = 3;
    low.category = "Avertissement";
    m.project().alarms.push_back(low);
    m.add(Kind::History, "Alarmes_En_Cours", 10, 10, 580, 180);
    m.title("Valeur_Pression", 10, 205, 580, "Pression : {Pression:0.0} bar", 18);
    m.note("Explication", 10, 245, 580, 50,
           "Les alarmes en cours : Pression_Haute au-dessus de 30 bar, Pression_Basse sous 12 bar.");
    return m.done(12);
}

Example trend() {
    Maker m;
    m.var("Pression", "REAL", "20.0");
    m.var("Consigne", "REAL", "22.0");
    m.cyclic("Pression := 20.0 + 8.0 * SIN(T * 0.9) + 2.0 * SIN(T * 3.1);");
    auto& c = m.add(Kind::Trend, "Courbe_Pression", 10, 10, 580, 280);
    c.set("variables", "Pression;Consigne");
    c.set("names", "Pression;Consigne");
    c.setNumber("duration", 20);
    c.setNumber("ymin", 0);
    c.setNumber("ymax", 40);
    return m.done(20);
}

Example list() {
    Maker m;
    m.var("Gaz", "STRING", "'Argon Ar'");
    auto& l = m.add(Kind::List, "Liste_Gaz", 40, 30, 240, 170);
    l.set("items", "Azote N2;Argon Ar;H\xC3\xA9lium He;Oxyg\xC3\xA8ne O2;M\xC3\xA9lange N2/Ar;Dioxyde de carbone");
    l.set("variable", "Gaz");
    m.title("Choisi", 320, 30, 250, "Gaz : {Gaz}", 18);
    m.note("Explication", 320, 80, 250, 190,
           "1.12.2 : un clic choisit une ligne ; elle se surligne et s'\xC3\xA9" "crit dans la variable (son libell\xC3\xA9 pour une "
           "STRING). Au-del\xC3\xA0 de ce qui tient, la liste d\xC3\xA9" "file. \xC2\xAB \xC3\x89l\xC3\xA9ments depuis \xC2\xBB : "
           "une \xC3\xA9num\xC3\xA9ration, une variable LIST, une MAP, un tableau.");
    m.step(1.0, "partie", "Liste_Gaz", "choix:3");
    m.step(3.0, "partie", "Liste_Gaz", "choix:0");
    return m.done(10);
}

Example video() {
    Maker m;
    m.svg("affiche.svg", posterSvg());
    // Une video n'est pas decodee : sa fiche (taille, duree) et son image d'attente.
    Resource r;
    r.id = m.project().allocate();
    r.name = "presentation.mp4";
    r.format = "MP4";
    r.width = 640;
    r.height = 360;
    r.seconds = 6;
    r.detail = "H.264 (avc1)";
    m.project().assets.resources.push_back(r);
    auto& v = m.add(Kind::Video, "Video_Presentation", 110, 20, 380, 214);
    v.set("video", "presentation.mp4");
    v.set("poster", "affiche.svg");
    m.note("Explication", 110, 245, 380, 50, "L'image d'attente, et la barre de lecture qui avance en marche.");
    return m.done(12);
}

Example container() {
    Maker m;
    m.var("X", "REAL", "500.0");
    m.cyclic("X := X - 3.0;\nIF X < -560.0 THEN X := 500.0; END_IF;");
    auto& box = m.add(Kind::Container, "Bandeau_Messages", 100, 70, 400, 90);
    const Id parent = box.id;
    auto& t = m.add(Kind::Text, "Message_Defilant", 500, 95, 660, 40);
    t.parent = parent;
    t.set("text", "Bouteille A presque vide : basculer sur B   \xC2\xB7   Pression B : 24 bar");
    t.setNumber("fontSize", 20);
    t.set("textColor", "#F2994A");
    t.setExpr("x", "X");
    m.note("Explication", 100, 190, 400, 100,
           "Le texte est DANS le conteneur, qui rogne ce qui d\xC3\xA9passe (Rogner le contenu) : un bandeau d\xC3\xA9" "filant.");
    return m.done(12);
}

Example recipeManager() {
    Maker m;
    m.var("Seuil_A", "REAL", "12.0");
    m.var("Seuil_B", "REAL", "12.0");
    m.var("Gaz", "STRING", "'Azote N2'");
    Recipe r;
    r.id = m.project().allocate();
    r.name = "Reglages";
    r.fields = {{"Seuil A", "Seuil_A", "kg", "0", "60"}, {"Seuil B", "Seuil_B", "kg", "0", "60"}, {"Gaz", "Gaz", "", "", ""}};
    const auto rec = [&](const char* name, const char* a, const char* b, const char* gas) {
        RecipeRecord x;
        x.id = m.project().allocate();
        x.name = name;
        x.values = {a, b, gas};
        r.records.push_back(x);
    };
    rec("Azote", "12", "12", "'Azote N2'");
    rec("Argon", "9", "9.5", "'Argon Ar'");
    rec("H\xC3\xA9lium", "5", "5", "'H\xC3\xA9lium He'");
    m.project().recipes.push_back(r);
    auto& g = m.add(Kind::RecipeManager, "Jeux_Reglages", 10, 10, 580, 190);
    g.set("recipe", "Reglages");
    g.set("buttons", "Appliquer;Lire");
    m.title("Installation", 10, 220, 580, "Installation : A = {Seuil_A:0.0} kg   B = {Seuil_B:0.0} kg   {Gaz}", 17);
    m.step(1.0, "partie", "Jeux_Reglages", "ligne:1");
    m.step(2.2, "partie", "Jeux_Reglages", "bouton:Appliquer");
    m.step(4.5, "partie", "Jeux_Reglages", "ligne:2");
    m.step(5.7, "partie", "Jeux_Reglages", "bouton:Appliquer");
    m.step(8.0, "partie", "Jeux_Reglages", "ligne:0");
    m.step(9.2, "partie", "Jeux_Reglages", "bouton:Appliquer");
    return m.done(11);
}

Example animatedImage() {
    Maker m;
    m.svg("pompe_arret.svg", pumpSvg(0, "#5A6474"));
    m.svg("pompe_1.svg", pumpSvg(0, "#2F6FD6"));
    m.svg("pompe_2.svg", pumpSvg(40, "#2F6FD6"));
    m.svg("pompe_3.svg", pumpSvg(80, "#2F6FD6"));
    m.svg("pompe_defaut.svg", pumpSvg(0, "#E5534B"));
    m.var("Marche", "BOOL", "FALSE");
    m.var("Defaut", "BOOL", "FALSE");
    m.cyclic("Marche := (REAL_TO_INT(T) MOD 8) < 5;\nDefaut := (REAL_TO_INT(T) MOD 8) = 7;");
    auto& p = m.add(Kind::AnimatedImage, "Pompe_P1", 60, 40, 200, 200);
    p.set("states", formatImageStates({{"Defaut", {"pompe_defaut.svg"}, 0},
                                       {"Marche", {"pompe_1.svg", "pompe_2.svg", "pompe_3.svg"}, 120}}));
    p.set("image", "pompe_arret.svg");
    m.title("Etat_Pompe", 300, 70, 290, "Pompe P1 : {Marche:en marche|arr\xC3\xAAt\xC3\xA9" "e}");
    m.note("Explication", 300, 130, 290, 150,
           "Des \xC3\xA9tats : Defaut \xE2\x86\x92 une image rouge ; Marche \xE2\x86\x92 trois images qui d\xC3\xA9" "filent (la roue tourne) ; "
           "sinon l'image par d\xC3\xA9" "faut. Le premier \xC3\xA9tat vrai l'emporte.");
    return m.done(8);
}

// Lot 16 : le GIF anime - Jouer, Pause, Arreter, Rejouer 3 fois ; ce que le moteur en dit.
Example animatedGif() {
    Maker m;
    auto blob = std::make_shared<Bytes>(fanGif(120, 24, 40));
    m.project().assets.resources.push_back(makeResource(m.project(), "ventilateur.gif", blob, "exemple"));
    auto& g = m.add(Kind::AnimatedGif, "Ventilateur", 40, 40, 150, 150);
    g.set("image", "ventilateur.gif");
    g.set("start", "sur action");
    const struct {
        const char* name;
        const char* label;
        Operation   op;
        const char* value;
        double      x;
    } buttons[] = {
        {"Btn_Jouer", "Jouer", Operation::GifPlay, "", 220},
        {"Btn_Pause", "Pause", Operation::GifPause, "", 310},
        {"Btn_Arreter", "Arr\xC3\xAAter", Operation::GifStop, "", 400},
        {"Btn_Rejouer", "3 tours", Operation::GifReplay, "3", 490},
    };
    for (const auto& b : buttons) {
        auto& btn = m.add(Kind::Button, b.name, b.x, 40, 84, 40);
        btn.set("text", b.label);
        btn.setNumber("fontSize", 15);
        btn.actions.push_back(click(b.op, "Ventilateur", b.value));
    }
    m.title("Etat_GIF", 220, 100, 360, "{Exemple.Ventilateur.Playing:joue|arr\xC3\xAAt\xC3\xA9} \xC2\xB7 image {Exemple.Ventilateur.Frame} / "
                                         "{Exemple.Ventilateur.FrameCount} \xC2\xB7 tours {Exemple.Ventilateur.Loops}", 16);
    m.note("Explication", 220, 150, 360, 130,
           "Le GIF part sur action : Jouer le lance (ou le reprend), Pause le fige sur l'image montr\xC3\xA9" "e, Arr\xC3\xAAter le ram\xC3\xA8ne "
           "\xC3\xA0 la premi\xC3\xA8re image, 3 tours le rejoue trois fois puis il reste sur sa derni\xC3\xA8re image.");
    m.step(1.0, "clic", "Btn_Jouer");
    m.step(4.0, "clic", "Btn_Pause");
    m.step(5.5, "clic", "Btn_Jouer");
    m.step(8.0, "clic", "Btn_Arreter");
    m.step(9.0, "clic", "Btn_Rejouer");
    return m.done(14);
}

Example inputField() {
    Maker m;
    m.var("Consigne", "REAL", "12.5");
    // Le champ en haut : le clavier virtuel s'ouvre en bas, au milieu.
    auto& f = m.add(Kind::InputField, "Saisie_Consigne", 30, 20, 200, 40);
    f.set("variable", "Consigne");
    f.set("min", "0");
    f.set("max", "40");
    f.set("unit", "bar");
    f.set("keyboard", "num\xC3\xA9rique");
    m.title("Consigne_Ecrite", 250, 24, 330, "Consigne \xC3\xA9" "crite : {Consigne:0.0} bar", 16).set("textColor", "#9CC3E6");
    m.step(1.0, "partie", "Saisie_Consigne", "champ");
    m.step(2.0, "clavier", "", "1");
    m.step(2.6, "clavier", "", "8");
    m.step(3.2, "clavier", "", ",");
    m.step(3.8, "clavier", "", "5");
    m.step(4.6, "clavier", "", "\xE2\x86\xB5");
    m.step(6.5, "partie", "Saisie_Consigne", "champ");
    m.step(7.3, "clavier", "", "4");
    m.step(7.9, "clavier", "", "5");
    m.step(8.7, "clavier", "", "\xE2\x86\xB5");
    m.step(10.5, "clavier", "", "\xC3\x89" "chap");
    return m.done(12);
}

Example loginPanel() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.user("admin", "", kAdministrateur, "admin");
    m.secure("");
    m.add(Kind::LoginPanel, "Connexion", 20, 30);
    auto& who = m.add(Kind::UserInfo, "Utilisateur", 380, 40, 210, 44);
    who.set("display", "{nom} ({niveau})");
    m.note("Explication", 380, 110, 210, 180,
           "Le mot de passe se tape masqu\xC3\xA9. R\xC3\xA9ussie, la connexion change le niveau d'acc\xC3\xA8s ; rat\xC3\xA9" "e, elle le dit.");
    m.step(1.0, "partie", "Connexion", "champ:motdepasse");
    m.step(1.8, "texte", "", "1");
    m.step(2.1, "texte", "", "2");
    m.step(2.4, "texte", "", "3");
    m.step(2.7, "texte", "", "4");
    m.step(3.5, "partie", "Connexion", "bouton");
    m.step(6.0, "partie", "Connexion", "suivant");
    m.step(6.8, "texte", "", "0000");
    m.step(7.6, "partie", "Connexion", "bouton");
    return m.done(10);
}

Example logoutButton() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.secure("operateur");
    auto& who = m.add(Kind::UserInfo, "Utilisateur", 30, 40, 400, 44);
    who.set("display", "{nom}  ({groupe}, niveau {niveau})");
    auto& b = m.add(Kind::LogoutButton, "Deconnexion", 30, 120, 260, 46);
    b.setFlag("confirm", true);
    m.note("Explication", 320, 110, 270, 170,
           "Demander confirmation : le premier clic demande \xC2\xAB Confirmer ? \xC2\xBB (3 s), le second d\xC3\xA9" "connecte.");
    m.step(2.0, "partie", "Deconnexion", "bouton");
    m.step(3.4, "partie", "Deconnexion", "bouton");
    return m.done(7);
}

Example userInfo() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.secure("operateur");
    m.project().security.autoLogoutMin = 1;
    auto& a = m.add(Kind::UserInfo, "Utilisateur", 30, 30, 540, 44);
    a.set("display", "{nom}  ({groupe}, niveau {niveau})");
    auto& b = m.add(Kind::UserInfo, "Duree_Connexion", 30, 94, 540, 44);
    b.set("display", "connect\xC3\xA9 depuis {depuis}, d\xC3\xA9" "connexion automatique dans {reste}");
    b.setFlag("icon", false);
    auto& c = m.add(Kind::UserInfo, "Identifiant", 30, 158, 300, 44);
    c.set("display", "identifiant : {login}");
    c.setFlag("icon", false);
    m.note("Explication", 30, 220, 540, 70, "Un gabarit : {{nom}}, {{login}}, {{groupe}}, {{niveau}}, {{depuis}}, {{reste}}.");
    m.step(6.0, "deconnecter");
    return m.done(9);
}

Example passwordChange() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.secure("operateur");
    auto& p = m.add(Kind::PasswordChange, "Mot_De_Passe", 20, 5);
    p.setFlag("requireDigit", true);
    m.note("Explication", 380, 20, 210, 260,
           "L'ancien mot de passe, le nouveau (6 caract\xC3\xA8res au moins, un chiffre), la confirmation. "
           "Une confirmation fausse est refus\xC3\xA9" "e ; le mot de passe n'est jamais gard\xC3\xA9 en clair.");
    m.step(1.0, "partie", "Mot_De_Passe", "champ:ancien");
    m.step(1.5, "texte", "", "1234");
    m.step(2.3, "partie", "Mot_De_Passe", "champ:nouveau");
    m.step(2.8, "texte", "", "Abc2027");
    m.step(3.6, "partie", "Mot_De_Passe", "champ:confirmation");
    m.step(4.1, "texte", "", "Abc2028");
    m.step(4.9, "partie", "Mot_De_Passe", "bouton");
    m.step(6.6, "partie", "Mot_De_Passe", "champ:confirmation");
    m.step(7.1, "texte", "", "Abc2027");
    m.step(7.9, "partie", "Mot_De_Passe", "bouton");
    return m.done(11);
}

Example userManager() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.user("chef", "Claire Dubois", kSuperviseur, "chef");
    m.user("admin", "", kAdministrateur, "admin");
    m.user("stagiaire", "", kOperateur, "stage");
    m.secure("admin");
    m.add(Kind::UserManager, "Comptes", 10, 10, 580, 280);
    m.step(1.5, "partie", "Comptes", "ligne:3");
    m.step(2.7, "partie", "Comptes", "bouton:Activer");
    m.step(5.2, "partie", "Comptes", "bouton:Activer");
    m.step(7.0, "partie", "Comptes", "ligne:1");
    return m.done(9);
}

// ---- lot 9 : les commandes ---------------------------------------------------------------
// Un glisser : la poignee suit le pointeur de la fraction f0 a f1 de sa course,
// entre t0 et t1, puis elle est lachee (la valeur est ecrite).
void drag(Maker& m, const std::string& object, double t0, double t1, double f0, double f1) {
    const int n = std::max(2, static_cast<int>((t1 - t0) / 0.1));
    for (int k = 0; k <= n; ++k) {
        const double u = static_cast<double>(k) / n;
        const double e = u * u * (3 - 2 * u);
        char b[32];
        std::snprintf(b, sizeof b, "%.3f", f0 + (f1 - f0) * e);
        m.step(t0 + (t1 - t0) * u, k == n ? "lacher" : "tirer", object, b);
    }
}

Example pushButton() {
    Maker m;
    m.var("Avance", "BOOL", "FALSE");
    m.var("Position", "REAL", "0.0");
    m.cyclic("IF Avance THEN Position := Position + 3.0; END_IF;\nIF Position > 430.0 THEN Position := 0.0; END_IF;");
    auto& b = m.add(Kind::PushButton, "Pas_A_Pas", 40, 50, 190, 56);
    b.set("text", "Pas \xC3\xA0 pas");
    b.set("variable", "Avance");
    auto& lamp = m.add(Kind::Indicator, "Voyant_Avance", 260, 58, 40, 40);
    lamp.setExpr("value", "Avance");
    m.title("Etat", 315, 62, 270, "{Avance:Avance...|\xC3\x80 l'arr\xC3\xAAt}", 18);
    auto& rail = m.add(Kind::Rectangle, "Rail", 40, 176, 520, 8);
    rail.set("fill", "#3A4556");
    rail.set("stroke", "");
    auto& chariot = m.add(Kind::Rectangle, "Chariot", 40, 140, 90, 40);
    chariot.set("fill", "#2F6FD6");
    chariot.setNumber("radius", 4);
    chariot.setExpr("x", "40.0 + Position");
    m.note("Explication", 40, 205, 520, 80,
           "Vrai tant qu'on appuie, faux au rel\xC3\xA2" "chement (l'inverse avec \xC2\xAB Impulsion invers\xC3\xA9" "e \xC2\xBB) : la variable Avance "
           "fait avancer le chariot par \xC3\xA0-coups.");
    m.step(1.0, "appui", "Pas_A_Pas");
    m.step(3.0, "relache", "Pas_A_Pas");
    m.step(4.5, "appui", "Pas_A_Pas");
    m.step(5.3, "relache", "Pas_A_Pas");
    m.step(6.4, "appui", "Pas_A_Pas");
    m.step(7.1, "relache", "Pas_A_Pas");
    return m.done(9);
}

Example switchControl() {
    Maker m;
    m.var("Marche", "BOOL", "FALSE");
    m.var("Tourne", "BOOL", "FALSE");
    m.var("Delai", "REAL", "0.0");
    m.var("Eclairage", "BOOL", "FALSE");
    m.cyclic("IF Marche <> Tourne THEN\n    Delai := Delai + 0.05;\n    IF Delai >= 1.2 THEN\n        Tourne := Marche;\n"
             "        Delai := 0.0;\n    END_IF;\nELSE\n    Delai := 0.0;\nEND_IF;");
    auto& a = m.add(Kind::Switch, "Pompe", 40, 40, 230, 48);
    a.set("variable", "Marche");
    a.set("state", "Tourne");
    a.setNumber("fontSize", 18);
    auto& b = m.add(Kind::Switch, "Eclairage", 40, 120, 230, 48);
    b.set("variable", "Eclairage");
    b.set("textOn", "Allum\xC3\xA9");
    b.set("textOff", "\xC3\x89teint");
    b.set("colorOn", "#F2C94C");
    b.setNumber("fontSize", 18);
    m.title("Commande", 310, 46, 280, "Commande : {Marche:marche|arr\xC3\xAAt}", 16);
    m.title("Retour", 310, 80, 280, "Retour : {Tourne:tourne|arr\xC3\xAAt\xC3\xA9" "e}", 16);
    m.note("Explication", 40, 200, 520, 90,
           "Un clic bascule la variable. Le retour d'\xC3\xA9tat (Tourne) suit 1,2 s plus tard : entre les deux, l'interrupteur "
           "montre la discordance par un cadre orange.");
    m.step(1.0, "clic", "Pompe");
    m.step(3.2, "clic", "Eclairage");
    m.step(5.0, "clic", "Pompe");
    m.step(7.0, "clic", "Eclairage");
    return m.done(9);
}

Example illuminatedButton() {
    Maker m;
    m.var("Lampe_1", "BOOL", "FALSE");
    m.var("Lampe_2", "BOOL", "FALSE");
    m.var("Lampe_3", "BOOL", "FALSE");
    auto& b1 = m.add(Kind::IlluminatedButton, "Btn_Basculer", 30, 40, 170, 60);
    b1.set("text", "Basculer");
    b1.set("variable", "Lampe_1");
    auto& b2 = m.add(Kind::IlluminatedButton, "Btn_Impulsion", 215, 40, 170, 60);
    b2.set("text", "Impulsion");
    b2.set("variable", "Lampe_2");
    b2.set("operation", "impulsion");
    b2.set("colorOn", "#F2C94C");
    auto& b3 = m.add(Kind::IlluminatedButton, "Btn_Marche", 400, 40, 170, 60);
    b3.set("text", "Mettre \xC3\xA0 1");
    b3.set("variable", "Lampe_3");
    b3.set("operation", "mettre \xC3\xA0 1");
    b3.set("colorOn", "#2F6FD6");
    auto& b4 = m.add(Kind::IlluminatedButton, "Btn_Arret", 400, 115, 170, 50);
    b4.set("text", "Mettre \xC3\xA0 0");
    b4.set("variable", "Lampe_3");
    b4.set("operation", "mettre \xC3\xA0 0");
    b4.set("lamp", "NOT Lampe_3");
    b4.set("colorOn", "#8A3A3A");
    m.note("Explication", 30, 190, 540, 100,
           "Le bouton porte son voyant : il s'allume avec sa variable (ou avec l'expression \xC2\xAB Voyant \xC2\xBB). Op\xC3\xA9rations : "
           "basculer, impulsion (vrai tant qu'on appuie), mettre \xC3\xA0 1, mettre \xC3\xA0 0.");
    m.step(1.0, "clic", "Btn_Basculer");
    m.step(2.3, "appui", "Btn_Impulsion");
    m.step(3.3, "relache", "Btn_Impulsion");
    m.step(4.5, "clic", "Btn_Marche");
    m.step(6.2, "clic", "Btn_Arret");
    m.step(7.6, "clic", "Btn_Basculer");
    return m.done(9);
}

Example selector() {
    Maker m;
    m.var("Mode", "INT", "1");
    m.var("Vitesse", "INT", "0");
    auto& s = m.add(Kind::Selector, "Mode", 20, 20, 260, 150);
    s.set("variable", "Mode");
    auto& v = m.add(Kind::Selector, "Vitesse", 310, 60, 270, 50);
    v.set("positions", "Lente;Moyenne;Rapide");
    v.set("style", "boutons");
    v.set("variable", "Vitesse");
    m.title("Etat", 310, 130, 280, "Mode {Mode}, vitesse {Vitesse}", 16);
    m.note("Explication", 20, 195, 560, 95,
           "Un clic sur une position l'\xC3\xA9" "crit (son rang, ou la valeur de \xC2\xAB Valeurs \xC3\xA9" "crites \xC2\xBB) ; un clic sur le bouton "
           "rotatif passe \xC3\xA0 la suivante. Le style \xC2\xAB boutons \xC2\xBB aligne les positions.");
    m.step(1.0, "partie", "Mode", "position:2");
    m.step(2.5, "partie", "Mode", "position:0");
    m.step(4.0, "partie", "Mode", "suivant");
    m.step(5.5, "partie", "Vitesse", "position:2");
    m.step(7.0, "partie", "Vitesse", "position:0");
    return m.done(8.5);
}

Example slider() {
    Maker m;
    m.var("Consigne", "REAL", "20.0");
    m.var("Niveau", "REAL", "30.0");
    auto& h = m.add(Kind::Slider, "Consigne", 30, 40, 380, 64);
    h.set("variable", "Consigne");
    h.set("unit", "%");
    auto& v = m.add(Kind::Slider, "Niveau", 470, 20, 90, 260);
    v.set("orientation", "verticale");
    v.setFlag("continuous", true);
    v.set("variable", "Niveau");
    v.set("unit", "%");
    auto& bar = m.add(Kind::ProgressBar, "Barre", 30, 130, 380, 26);
    bar.setExpr("value", "Consigne");
    m.title("Valeurs", 30, 170, 420, "Consigne {Consigne:0} %, niveau {Niveau:0} %", 16);
    m.note("Explication", 30, 210, 420, 80,
           "La poign\xC3\xA9" "e suit le pointeur ; la valeur (au pas, dans les bornes) est \xC3\xA9" "crite au rel\xC3\xA2" "chement, ou en glissant "
           "avec \xC2\xAB \xC3\x89" "crire en glissant \xC2\xBB (le curseur vertical).");
    drag(m, "Consigne", 1.0, 2.6, 0.2, 0.8);
    drag(m, "Niveau", 4.0, 5.6, 0.3, 0.85);
    drag(m, "Consigne", 6.5, 7.5, 0.8, 0.45);
    return m.done(9);
}

Example knob() {
    Maker m;
    m.var("Pression", "REAL", "10.0");
    auto& k = m.add(Kind::Knob, "Consigne_Pression", 40, 30, 210, 210);
    k.set("variable", "Pression");
    k.setNumber("max", 40);
    k.setNumber("step", 0.5);
    k.set("format", "0.0");
    k.set("unit", "bar");
    auto& g = m.add(Kind::Gauge, "Jauge", 330, 30, 210, 210);
    g.setExpr("value", "Pression");
    g.setNumber("max", 40);
    g.set("unit", "bar");
    m.note("Explication", 40, 250, 520, 45, "Tourner : 270\xC2\xB0 de course (le bas reste libre), au pas de 0,5 bar ; la jauge suit.");
    drag(m, "Consigne_Pression", 1.0, 2.8, 0.25, 0.8);
    drag(m, "Consigne_Pression", 4.5, 5.5, 0.8, 0.45);
    return m.done(7.5);
}

Example comboBox() {
    Maker m;
    m.var("Gaz", "STRING", "'Azote'");
    auto& c = m.add(Kind::ComboBox, "Gaz", 40, 40, 240, 38);
    c.set("items", "Azote;Argon;H\xC3\xA9lium;Oxyg\xC3\xA8ne;CO2;Air comprim\xC3\xA9;Hydrog\xC3\xA8ne");
    c.setNumber("maxVisible", 4);
    c.set("variable", "Gaz");
    m.title("Choisi", 320, 44, 260, "Gaz : {Gaz}", 18);
    m.note("Explication", 320, 100, 260, 170,
           "Un clic ouvre la liste sous l'objet ; au-del\xC3\xA0 de \xC2\xAB Lignes visibles \xC2\xBB, les fl\xC3\xA8" "ches la font d\xC3\xA9" "filer. Le choix "
           "\xC3\xA9" "crit son libell\xC3\xA9 (une STRING), son rang, ou la valeur de \xC2\xAB Valeurs \xC3\xA9" "crites \xC2\xBB.");
    m.step(1.0, "partie", "Gaz", "ouvrir");
    m.step(2.0, "partie", "Gaz", "defiler:1");
    m.step(2.5, "partie", "Gaz", "defiler:1");
    m.step(3.4, "partie", "Gaz", "choix:4");
    m.step(5.5, "partie", "Gaz", "ouvrir");
    m.step(6.5, "partie", "Gaz", "choix:1");
    return m.done(8.5);
}

Example checkBox() {
    Maker m;
    m.var("Purge", "BOOL", "FALSE");
    m.var("Sonore", "BOOL", "TRUE");
    m.var("Eco", "BOOL", "FALSE");
    auto& c1 = m.add(Kind::CheckBox, "Purge_Auto", 40, 40, 300, 34);
    c1.set("text", "Purge automatique");
    c1.set("variable", "Purge");
    auto& c2 = m.add(Kind::CheckBox, "Alarme_Sonore", 40, 90, 300, 34);
    c2.set("text", "Alarme sonore");
    c2.set("variable", "Sonore");
    auto& c3 = m.add(Kind::CheckBox, "Economie", 40, 140, 300, 34);
    c3.set("text", "Mode \xC3\xA9" "conomie");
    c3.set("variable", "Eco");
    m.title("Purge", 370, 44, 220, "Purge : {Purge:oui|non}", 16);
    m.title("Sonore", 370, 94, 220, "Alarme : {Sonore:oui|non}", 16);
    m.title("Eco", 370, 144, 220, "\xC3\x89" "conomie : {Eco:oui|non}", 16);
    m.note("Explication", 40, 200, 520, 85, "Un clic coche ou d\xC3\xA9" "coche : la variable (BOOL) bascule. Un retour d'\xC3\xA9tat la montrerait telle qu'elle est.");
    m.step(1.0, "clic", "Purge_Auto");
    m.step(2.3, "clic", "Alarme_Sonore");
    m.step(3.6, "clic", "Economie");
    m.step(5.2, "clic", "Purge_Auto");
    return m.done(7);
}

Example radioGroup() {
    Maker m;
    m.var("Vitesse", "INT", "2");
    m.var("Mode", "INT", "0");
    auto& r1 = m.add(Kind::RadioGroup, "Vitesse", 40, 30, 240, 120);
    r1.set("variable", "Vitesse");
    auto& r2 = m.add(Kind::RadioGroup, "Mode", 320, 40, 260, 40);
    r2.set("items", "Manu;Auto");
    r2.set("orientation", "horizontale");
    r2.set("variable", "Mode");
    m.title("Etat", 320, 100, 260, "Vitesse {Vitesse}, mode {Mode}", 16);
    m.note("Explication", 40, 180, 520, 100,
           "L'option choisie \xC3\xA9" "crit sa valeur (son rang, ou celle de \xC2\xAB Valeurs \xC3\xA9" "crites \xC2\xBB) ; la variable choisit l'option montr\xC3\xA9" "e.");
    m.step(1.0, "partie", "Vitesse", "option:1");
    m.step(2.5, "partie", "Vitesse", "option:0");
    m.step(4.0, "partie", "Mode", "option:1");
    m.step(5.5, "partie", "Vitesse", "option:2");
    m.step(6.8, "partie", "Mode", "option:0");
    return m.done(8);
}

Example dateTimePicker() {
    Maker m;
    m.var("Echeance", "STRING", "''");
    auto& p = m.add(Kind::DateTimePicker, "Echeance", 40, 40, 420, 100);
    p.set("variable", "Echeance");
    m.title("Enregistree", 40, 160, 520, "\xC3\x89" "ch\xC3\xA9" "ance enregistr\xC3\xA9" "e : {Echeance}", 16);
    m.note("Explication", 40, 205, 520, 85,
           "Les fl\xC3\xA8" "ches r\xC3\xA8glent chaque champ (le jour se ram\xC3\xA8ne au dernier du mois) ; Maintenant reprend l'heure du poste ; "
           "Valider \xC3\xA9" "crit (une STRING 2026-09-23 14:30, un TIME, ou des secondes depuis 1970).");
    m.step(1.0, "partie", "Echeance", "plus:jour");
    m.step(1.4, "partie", "Echeance", "plus:jour");
    m.step(1.8, "partie", "Echeance", "plus:jour");
    m.step(2.5, "partie", "Echeance", "plus:heure");
    m.step(3.1, "partie", "Echeance", "moins:minute");
    m.step(3.5, "partie", "Echeance", "moins:minute");
    m.step(4.4, "partie", "Echeance", "valider");
    m.step(6.0, "partie", "Echeance", "maintenant");
    m.step(6.8, "partie", "Echeance", "valider");
    return m.done(9);
}

Example weeklySchedule() {
    Maker m;
    m.var("Plages", "STRING", "''");
    m.var("Chauffe", "BOOL", "FALSE");
    auto& s = m.add(Kind::WeeklySchedule, "Chauffage", 10, 10, 580, 222);
    s.set("variable", "Plages");
    s.set("output", "Chauffe");
    s.set("resolution", "1 h");
    s.set("schedule", "Lu-Ve=07:00-12:00,13:00-17:00");      // les heures de bureau, a l'heure pres
    auto& lamp = m.add(Kind::Indicator, "Voyant_Chauffe", 20, 248, 34, 34);
    lamp.setExpr("value", "Chauffe");
    auto& t = m.title("Etat", 66, 250, 520, "Chauffe {Chauffe:en plage|hors plage} : {Plages}", 13);
    t.setFlag("wrap", true);
    m.step(1.0, "partie", "Chauffage", "jour:5");
    m.step(2.5, "partie", "Chauffage", "case:2,19");
    m.step(4.0, "partie", "Chauffage", "heure:20");
    m.step(5.5, "partie", "Chauffage", "jour:5");
    return m.done(7.5);
}

// ---- lot 9 : les afficheurs --------------------------------------------------------------
Example numericDisplay() {
    Maker m;
    m.var("Pression", "REAL", "25.0");
    m.var("Debit", "REAL", "120.0");
    m.cyclic("Pression := 25.0 + 24.0 * SIN(T * 0.7);\nDebit := 120.0 + 80.0 * SIN(T * 0.35);");
    auto& a = m.add(Kind::NumericDisplay, "Pression", 40, 40, 250, 70);
    a.set("variable", "Pression");
    a.set("unit", "bar");
    a.set("label", "Pression");
    a.set("lowAlarm", "5");
    a.set("low", "10");
    a.set("high", "40");
    a.set("highAlarm", "45");
    a.setNumber("fontSize", 28);
    auto& b = m.add(Kind::NumericDisplay, "Debit", 320, 40, 240, 70);
    b.set("variable", "Debit");
    b.set("format", "0");
    b.set("unit", "kg/h");
    b.set("label", "D\xC3\xA9" "bit");
    b.set("high", "180");
    b.setNumber("fontSize", 28);
    m.note("Explication", 40, 140, 520, 140,
           "La valeur de sa variable, au format voulu (0.0), avec son unit\xC3\xA9 et son libell\xC3\xA9. Sous le seuil bas ou au-dessus "
           "du seuil haut : la couleur des seuils ; au-del\xC3\xA0 des alarmes : la couleur des alarmes. Une valeur illisible "
           "s'affiche ###.");
    return m.done(10);
}

// Un compteur d'etats : Etat avance d'un cran toutes les deux secondes (0..last).
void stateCounter(Maker& m, int last) {
    m.var("Etat", "INT", "0");
    m.var("C", "INT", "0");
    m.cyclic("C := C + 1;\nIF C >= 40 THEN\n    C := 0;\n    Etat := Etat + 1;\n    IF Etat > " + std::to_string(last)
             + " THEN Etat := 0; END_IF;\nEND_IF;");
}

Example multiStateIndicator() {
    Maker m;
    stateCounter(m, 2);
    auto& a = m.add(Kind::MultiStateIndicator, "Etat_Pompe", 40, 50, 240, 48);
    a.set("variable", "Etat");
    auto& b = m.add(Kind::MultiStateIndicator, "Etat_Carre", 320, 50, 240, 48);
    b.set("variable", "Etat");
    b.set("shape", "carr\xC3\xA9");
    m.note("Explication", 40, 140, 520, 140,
           "Un \xC3\xA9tat par valeur : 0 = Arr\xC3\xAAt | #4A5261; 1 = Marche | #2ECC71; 2 = D\xC3\xA9" "faut | #E5534B | clignote. Une valeur "
           "peut \xC3\xAAtre un intervalle (10..20), un texte ('Auto') ou * (tout le reste).");
    return m.done(6.5);
}

Example multiStateText() {
    Maker m;
    stateCounter(m, 4);
    auto& a = m.add(Kind::MultiStateText, "Etat_Ligne", 40, 40, 520, 60);
    a.set("variable", "Etat");
    a.setNumber("fontSize", 26);
    a.set("fill", "#141820");
    a.set("stroke", "#3A4556");
    m.title("Valeur", 40, 115, 520, "Etat = {Etat}", 16);
    m.note("Explication", 40, 160, 520, 120,
           "Le texte (et sa couleur) de la valeur : 0 = Arr\xC3\xAAt; 1 = En marche | #2ECC71; 2 = En d\xC3\xA9" "faut | #E5534B | clignote; "
           "3 = Maintenance | #F2C94C. Une valeur sans \xC3\xA9tat : le texte par d\xC3\xA9" "faut (\xC3\x89tat inconnu).");
    return m.done(10);
}

Example bargraph() {
    Maker m;
    m.var("Niveau", "REAL", "50.0");
    m.var("Debit", "REAL", "50.0");
    m.cyclic("Niveau := 50.0 + 45.0 * SIN(T * 0.6);\nDebit := 50.0 + 40.0 * SIN(T * 0.9 + 1.0);");
    auto& a = m.add(Kind::Bargraph, "Niveau_Cuve", 40, 20, 90, 260);
    a.set("variable", "Niveau");
    a.set("unit", "%");
    auto& b = m.add(Kind::Bargraph, "Debit", 170, 50, 400, 76);
    b.set("orientation", "horizontale");
    b.set("variable", "Debit");
    b.set("setpoint", "70");
    b.set("unit", "%");
    m.note("Explication", 170, 160, 400, 120,
           "La valeur remplit la barre ; les zones colorent l'\xC3\xA9" "chelle (0-60 vert, 60-80 jaune, 80-100 rouge) ; la consigne "
           "est un rep\xC3\xA8re blanc.");
    return m.done(10.5);
}

Example thermometer() {
    Maker m;
    m.var("Temperature", "REAL", "20.0");
    m.cyclic("Temperature := 20.0 + 30.0 * SIN(T * 0.5);");
    auto& t = m.add(Kind::Thermometer, "Temperature", 60, 20, 90, 260);
    t.set("variable", "Temperature");
    auto& d = m.add(Kind::NumericDisplay, "Valeur", 200, 40, 220, 60);
    d.set("variable", "Temperature");
    d.set("unit", "\xC2\xB0" "C");
    d.set("label", "Temp\xC3\xA9rature");
    m.note("Explication", 200, 130, 380, 130,
           "La colonne monte avec la valeur, entre le minimum et le maximum (-20 \xC3\xA0 60 \xC2\xB0" "C), gradu\xC3\xA9" "e ; sa couleur, celle du "
           "remplissage.");
    return m.done(12.5);
}

Example dial() {
    Maker m;
    m.var("Vitesse", "REAL", "50.0");
    m.cyclic("Vitesse := 50.0 + 48.0 * SIN(T * 0.5);");
    auto& d = m.add(Kind::Dial, "Vitesse", 40, 20, 260, 260);
    d.set("variable", "Vitesse");
    d.set("label", "Vitesse");
    d.set("unit", "%");
    m.note("Explication", 330, 40, 250, 220,
           "L'aiguille sur l'\xC3\xA9" "chelle ; les zones colorent l'arc (0-60 vert, 60-85 jaune, 85-100 rouge). La valeur s'\xC3\xA9" "crit "
           "sous l'axe, avec son unit\xC3\xA9.");
    return m.done(12.5);
}

Example clock() {
    Maker m;
    m.add(Kind::Clock, "Horloge", 40, 20, 250, 250);
    auto& b = m.add(Kind::Clock, "Horloge_Numerique", 320, 50, 260, 110);
    b.set("clockStyle", "num\xC3\xA9rique");
    b.setNumber("fontSize", 32);
    m.note("Explication", 320, 190, 260, 90, "L'heure du poste, analogique ou num\xC3\xA9rique, avec la date.");
    return m.done(6);
}

Example hourMeter() {
    Maker m;
    m.var("Pompe", "BOOL", "FALSE");
    m.var("Heures_Pompe", "REAL", "0.0");
    auto& s = m.add(Kind::Switch, "Pompe", 40, 40, 200, 44);
    s.set("variable", "Pompe");
    auto& a = m.add(Kind::HourMeter, "Temps_Marche", 280, 30, 290, 64);
    a.set("condition", "Pompe");
    auto& b = m.add(Kind::HourMeter, "Heures", 280, 110, 290, 64);
    b.set("condition", "Pompe");
    b.set("durationFormat", "heures");
    b.set("variable", "Heures_Pompe");
    b.set("label", "En heures");
    m.note("Explication", 40, 200, 530, 85,
           "Il compte tant que sa condition est vraie ; sa variable (TIME, REAL en heures, ou un entier en secondes) garde "
           "le total, et un script peut la remettre \xC3\xA0 z\xC3\xA9ro.");
    m.step(1.0, "clic", "Pompe");
    m.step(5.5, "clic", "Pompe");
    return m.done(8);
}

Example sevenSegment() {
    Maker m;
    m.var("Compteur", "REAL", "0.0");
    m.cyclic("Compteur := Compteur + 0.35;\nIF Compteur > 999.9 THEN Compteur := 0.0; END_IF;");
    auto& a = m.add(Kind::SevenSegment, "Compteur", 40, 40, 300, 90);
    a.set("variable", "Compteur");
    auto& b = m.add(Kind::SevenSegment, "Zeros", 380, 40, 190, 90);
    b.set("variable", "Compteur");
    b.setNumber("decimals", 0);
    b.setFlag("leadingZeros", true);
    b.set("colorOn", "#2ECC71");
    b.set("colorOff", "#12301E");
    m.note("Explication", 40, 160, 520, 120,
           "Des chiffres de 7 segments : le nombre de chiffres, de d\xC3\xA9" "cimales, les z\xC3\xA9ros \xC3\xA0 gauche. Trop grand pour ses "
           "chiffres, ou illisible : des tirets.");
    return m.done(10);
}

Example trendArrow() {
    Maker m;
    m.var("Mesure", "REAL", "20.0");
    m.cyclic("IF T < 3.0 THEN\n    Mesure := Mesure + 0.4;\nELSIF T >= 6.0 AND T < 9.0 THEN\n    Mesure := Mesure - 0.4;\nEND_IF;");
    auto& a = m.add(Kind::TrendArrow, "Tendance", 60, 45, 90, 90);
    a.set("variable", "Mesure");
    a.setNumber("window", 2);
    auto& d = m.add(Kind::NumericDisplay, "Mesure", 190, 60, 220, 60);
    d.set("variable", "Mesure");
    m.note("Explication", 40, 170, 520, 110,
           "La valeur de maintenant contre celle d'il y a N secondes (la fen\xC3\xAAtre) : en hausse, en baisse, ou stable dans la "
           "zone morte.");
    return m.done(11);
}

Example marquee() {
    Maker m;
    m.var("Message", "STRING", "'Ligne 2 : changement de format \xC3\xA0 14 h'");
    auto& a = m.add(Kind::Marquee, "Bandeau", 20, 40, 560, 44);
    a.set("text", "Bienvenue - {Message} - consignes de s\xC3\xA9" "curit\xC3\xA9 au poste 3");
    a.setNumber("speed", 80);
    auto& b = m.add(Kind::Marquee, "Bandeau_Droite", 20, 110, 560, 40);
    b.set("text", "Il est {SYS.Time} - {SYS.AlarmCount} alarme(s) en cours");
    b.set("direction", "droite");
    b.setNumber("speed", 50);
    b.set("textColor", "#9CC3E6");
    b.setNumber("fontSize", 16);
    m.note("Explication", 20, 180, 560, 100,
           "Le texte d\xC3\xA9" "file en boucle, \xC3\xA0 sa vitesse (pixels par seconde), vers la gauche ou vers la droite ; un texte \xC3\xA0 "
           "trous se remplit en marche (ici une variable, et SYS.Time).");
    return m.done(10);
}

Example qrCode() {
    Maker m;
    m.var("Numero", "INT", "1");
    m.var("C", "INT", "0");
    m.cyclic("C := C + 1;\nIF C >= 60 THEN\n    C := 0;\n    Numero := Numero + 1;\n    IF Numero > 3 THEN Numero := 1; END_IF;\nEND_IF;");
    auto& q = m.add(Kind::QrCode, "Code", 40, 30, 230, 230);
    q.set("text", "https://example.com/armoire-{Numero}");
    m.title("Armoire", 300, 40, 280, "Armoire {Numero}", 20);
    m.note("Explication", 300, 100, 280, 180,
           "Le texte (ici une adresse, avec un trou) devient un code QR, recalcul\xC3\xA9 quand il change. La correction d'erreurs "
           "(L, M, Q, H) le garde lisible m\xC3\xAAme ab\xC3\xAEm\xC3\xA9.");
    return m.done(9);
}

// ---- lot 10 -------------------------------------------------------------------------
Example systemButton() {
    Maker m;
    m.add(Kind::SystemButton, "Btn_Parametres", 30, 40, 220, 50);
    m.title("Reglages", 280, 30, 300, "Luminosit\xC3\xA9 {SYS.Brightness} %", 18);
    m.title("Son", 280, 72, 300, "Son {SYS.SoundEnabled:activ\xC3\xA9|coup\xC3\xA9}, volume {SYS.Volume} %", 16);
    m.note("Explication", 30, 130, 540, 150,
           "Un clic ouvre le menu natif Param\xC3\xA8tres syst\xC3\xA8me, sans action \xC3\xA0 \xC3\xA9" "crire : les r\xC3\xA9glages du poste (luminosit\xC3\xA9, "
           "veille, son, volume, d\xC3\xA9" "connexion, clavier, heure) et le diagnostic. Chaque r\xC3\xA9glage se lit aussi en variable "
           "SYS. (SYS.Brightness, SYS.Volume, SYS.SoundEnabled...).");
    m.step(1.0, "clic", "Btn_Parametres");
    m.step(2.2, "menu", "", "moins:luminosite");
    m.step(2.9, "menu", "", "moins:luminosite");
    m.step(3.6, "menu", "", "moins:volume");
    m.step(4.3, "menu", "", "bascule:son");
    m.step(5.4, "menu", "", "onglet:diagnostic");
    m.step(7.6, "menu", "", "fermer");
    return m.done(10);
}

// ---- lot 10 : un symbole reutilisable, pose trois fois ------------------------------
//  Le symbole Carte_Moteur (son cadre, son nom, son moteur, sa vitesse, son
//  bouton) declare trois parametres ; chaque instance relie les siens a ses
//  variables. Le pointeur demarre M1, M3 puis M2 ; arrete M1 et M3.
Example symbolInstance() {
    Maker m;
    for (int i = 1; i <= 3; ++i) {
        m.var("Marche_" + std::to_string(i), "BOOL", "FALSE");
        m.var("Vitesse_" + std::to_string(i), "REAL", "0.0");
    }
    std::string ramp;
    for (int i = 1; i <= 3; ++i) {
        const std::string n = std::to_string(i);
        ramp += "IF Marche_" + n + " THEN Vitesse_" + n + " := MIN(1450.0, Vitesse_" + n + " + 45.0);\n"
                "ELSE Vitesse_" + n + " := MAX(0.0, Vitesse_" + n + " - 70.0); END_IF;\n";
    }
    m.cyclic(ramp);
    // Le symbole : une vue de role symbole, a cote de la vue de l'exemple.
    auto& p = m.project();
    View sym = makeView(p, "Carte_Moteur");
    sym.role = std::string(kSymbolRole);
    sym.width = 170;
    sym.height = 150;
    sym.background = "#1B2029";
    sym.params = {{"Marche", "Marche_1", "la commande et l'\xC3\xA9tat du moteur"},
                  {"Vitesse", "Vitesse_1", "sa vitesse (tr/min)"},
                  {"Nom", "'M1'", "le rep\xC3\xA8re affich\xC3\xA9"}};
    const auto put = [&](Kind k, const std::string& name, double x, double y, double w, double h) -> Object& {
        Object o = makeObject(k, p.allocate(), name, x, y, sym.activeLayer);
        o.setNumber("w", w);
        o.setNumber("h", h);
        sym.objects.push_back(std::move(o));
        return sym.objects.back();
    };
    {
        auto& cadre = put(Kind::Rectangle, "Cadre", 0, 0, 170, 150);
        cadre.set("fill", "#232A35");
        cadre.set("stroke", "#4A5568");
        cadre.setNumber("radius", 8);
    }
    {
        auto& titre = put(Kind::Text, "Titre", 8, 4, 154, 26);
        titre.set("text", "{Nom}");
        titre.set("align", "centre");
        titre.setNumber("fontSize", 16);
    }
    {
        auto& moteur = put(Kind::Motor, "Moteur", 50, 30, 70, 56);
        moteur.setExpr("value", "Marche");
        moteur.set("variable", "");
    }
    {
        auto& vitesse = put(Kind::Text, "Vitesse", 8, 88, 154, 22);
        vitesse.set("text", "{Vitesse:0} tr/min");
        vitesse.set("align", "centre");
        vitesse.setNumber("fontSize", 13);
        vitesse.set("textColor", "#9AA6B8");
    }
    {
        auto& bouton = put(Kind::Button, "Bouton", 30, 114, 110, 28);
        bouton.set("text", "{Marche:Arr\xC3\xAAter|D\xC3\xA9marrer}");
        bouton.setNumber("fontSize", 13);
        Action a;
        a.trigger = Trigger::Click;
        a.operation = Operation::Toggle;
        a.target = "Marche";
        bouton.actions.push_back(a);
    }
    // 1.9 : une alarme du symbole, ecrite avec ses parametres - chaque carte posee
    // la recoit, developpee avec ses arguments (Vue.Carte_1.Survitesse :
    // Vitesse_1 > 1500.0 ; la rampe s'arrete a 1450, elle ne monte pas d'elle-meme).
    {
        AlarmDef a;
        a.id = p.allocate();
        a.name = "Survitesse";
        a.condition = "Vitesse > 1500.0";
        a.message = "{Nom} : survitesse ({Vitesse:0} tr/min)";
        a.priority = 2;
        a.category = "Alarme";
        a.instruction = "Arr\xC3\xAAte le moteur {Nom} et v\xC3\xA9rifie sa consigne.";
        sym.alarms.push_back(std::move(a));
    }
    for (int i = 1; i <= 3; ++i) {
        const std::string n = std::to_string(i);
        auto& inst = m.add(Kind::SymbolInstance, "Carte_" + n, 25 + (i - 1) * 190, 16, 170, 150);
        inst.set("symbol", "Carte_Moteur");
        inst.set("params", "Marche := Marche_" + n + "; Vitesse := Vitesse_" + n + "; Nom := 'M" + n + "'");
    }
    m.note("Explication", 25, 184, 550, 110,
           "Un symbole dessin\xC3\xA9 une fois (Carte_Moteur : cadre, nom, moteur, vitesse, bouton), pos\xC3\xA9 trois fois. "
           "Chaque instance relie ses param\xC3\xA8tres \xC3\xA0 ses variables (Marche := Marche_2 ; Nom := 'M2') : "
           "le bouton de la carte 2 bascule Marche_2. Modifier le symbole modifie les trois cartes.");
    m.step(1.0, "clic", "Carte_1.Bouton");
    m.step(2.4, "clic", "Carte_3.Bouton");
    m.step(4.2, "clic", "Carte_2.Bouton");
    m.step(6.8, "clic", "Carte_1.Bouton");
    m.step(8.4, "clic", "Carte_3.Bouton");
    // En dernier : m.add() tient des references dans la vue de l'exemple.
    p.views.push_back(std::move(sym));
    return m.done(10);
}

// ---- lot 10 : les symboles de synoptique --------------------------------------------
//  Un meme scenario pour tous : a 1 s la marche, de 5,5 a 7 s un defaut (le
//  symbole clignote), a 8,5 s l'arret ; un niveau qui monte et descend, un
//  verin qui sort et rentre, une mesure qui oscille. Le symbole est a gauche,
//  ce qu'il montre ecrit a droite.
// ---- lot 11 : les graphiques ------------------------------------------------------------
AlarmDef& addAlarm(Project& p, const std::string& name, const std::string& condition, const std::string& message, int priority,
                   const std::string& group, const std::string& instruction = {}) {
    AlarmDef a;
    a.id = p.allocate();
    a.name = name;
    a.condition = condition;
    a.message = message;
    a.priority = priority;
    a.group = group;
    a.instruction = instruction;
    p.alarms.push_back(std::move(a));
    return p.alarms.back();
}

Example barChart() {
    Maker m;
    for (int k = 1; k <= 5; ++k) m.var("Cuve_" + std::to_string(k), "REAL", "40.0");
    m.cyclic("Cuve_1 := 50.0 + 30.0 * SIN(T * 0.6);\nCuve_2 := 62.0 + 30.0 * SIN(T * 0.45 + 1.0);\n"
             "Cuve_3 := 35.0 + 20.0 * SIN(T * 0.8 + 2.0);\nCuve_4 := 70.0 + 22.0 * SIN(T * 0.5 + 3.0);\n"
             "Cuve_5 := 25.0 + 15.0 * SIN(T * 0.9 + 4.0);");
    auto& b = m.add(Kind::BarChart, "Niveaux", 10, 10, 370, 280);
    b.set("text", "Niveau des cuves (%)");
    b.set("variables", "Cuve_1; Cuve_2; Cuve_3; Cuve_4; Cuve_5");
    b.set("names", "T1; T2; T3; T4; T5");
    b.set("unit", "%");
    b.set("format", "0");
    b.set("high", "85");
    b.set("low", "15");
    m.note("Explication", 395, 20, 195, 260,
           "Une barre par expression, \xC3\xA0 sa couleur. Au-dessus du seuil haut (85 %) ou sous le seuil bas, elle passe au rouge. "
           "La valeur s'\xC3\xA9" "crit au-dessus ; l'\xC3\xA9" "chelle est fixe (0 \xC3\xA0 100) ou automatique.");
    return m.done(14);
}

Example xyChart() {
    Maker m;
    m.var("Debit", "REAL", "0.0");
    m.var("Pression", "REAL", "9.0");
    m.cyclic("Debit := 50.0 - 50.0 * COS(T * 0.45);\n"
             "Pression := 9.1 - 0.00062 * Debit * Debit + 0.25 * SIN(T * 3.1) - 0.4 * (Debit / 100.0);");
    auto& x = m.add(Kind::XYChart, "Courbe_Pompe", 10, 10, 400, 280);
    x.set("text", "Pompe P-101 : pression / d\xC3\xA9" "bit");
    x.set("xVariable", "Debit");
    x.set("variables", "Pression");
    x.set("names", "Mesure");
    x.setNumber("ymin", 0);
    x.setNumber("ymax", 10);
    x.set("xLabel", "D\xC3\xA9" "bit (m\xC2\xB3/h)");
    x.set("yLabel", "Pression (bar)");
    x.set("reference", "0,9.2 20,8.9 40,8.1 60,6.8 80,5.0 100,2.6");
    x.setNumber("tolerance", 0.5);
    x.setNumber("maxPoints", 160);
    m.note("Explication", 420, 20, 170, 260,
           "Y en fonction de X : chaque cycle ajoute un point (d\xC3\xA9" "bit, pression). En pointill\xC3\xA9s, la courbe du constructeur et sa "
           "tol\xC3\xA9rance (\xC2\xB1 0,5 bar) : un point dehors se voit tout de suite.");
    return m.done(16);
}

Example stateChart() {
    Maker m;
    m.var("Pompe_1", "INT", "0");
    m.var("Pompe_2", "INT", "0");
    m.var("Vanne", "INT", "0");
    m.cyclic("Pompe_1 := SEL(SIN(T * 0.9) > 0.0, 0, 1);\n"
             "Pompe_2 := SEL(SIN(T * 0.55 + 1.0) > -0.2, 0, 1);\n"
             "IF T > 6.0 AND T < 8.0 THEN Pompe_2 := 2; END_IF;\n"
             "Vanne := SEL(Pompe_1 = 1 OR Pompe_2 = 1, 0, 1);");
    auto& s = m.add(Kind::StateChart, "Chronogramme", 10, 10, 580, 210);
    s.set("text", "Les 20 derni\xC3\xA8res secondes");
    s.set("variables", "Pompe_1; Pompe_2; Vanne");
    s.set("names", "Pompe P1; Pompe P2; Vanne V3");
    s.setNumber("duration", 20);
    s.set("stateList", "0 = Arr\xC3\xAAt | #4A5261; 1 = Marche | #2ECC71; 2 = D\xC3\xA9" "faut | #E5534B");
    m.note("Explication", 20, 228, 560, 70,
           "Une ligne par expression : chaque changement d'\xC3\xA9tat commence un nouveau segment, \xC3\xA0 la couleur de l'\xC3\xA9tat "
           "(Arr\xC3\xAAt, Marche, D\xC3\xA9" "faut). La fen\xC3\xAAtre glisse : \xC3\xA0 droite, maintenant.");
    return m.done(14);
}

Example pieChart() {
    Maker m;
    m.var("Atelier_A", "REAL", "30.0");
    m.var("Atelier_B", "REAL", "20.0");
    m.var("Atelier_C", "REAL", "15.0");
    m.var("Atelier_D", "REAL", "10.0");
    m.cyclic("Atelier_A := 320.0 + 60.0 * SIN(T * 0.5);\nAtelier_B := 210.0 + 50.0 * SIN(T * 0.7 + 1.0);\n"
             "Atelier_C := 150.0 + 40.0 * SIN(T * 0.9 + 2.0);\nAtelier_D := 90.0 + 30.0 * SIN(T * 0.6 + 3.0);");
    auto& a = m.add(Kind::PieChart, "Camembert", 10, 10, 285, 280);
    a.set("text", "\xC3\x89nergie par atelier");
    a.set("variables", "Atelier_A; Atelier_B; Atelier_C; Atelier_D");
    a.set("names", "Atelier A; Atelier B; Atelier C; Atelier D");
    a.set("legend", "FALSE");
    auto& d = m.add(Kind::PieChart, "Anneau", 305, 10, 285, 280);
    d.set("text", "Le m\xC3\xAAme, en anneau (kWh)");
    d.set("variables", "Atelier_A; Atelier_B; Atelier_C; Atelier_D");
    d.set("names", "A; B; C; D");
    d.setNumber("hole", 55);
    d.set("labelMode", "valeur");
    d.set("unit", "kWh");
    return m.done(14);
}

Example radarChart() {
    Maker m;
    for (const char* v : {"Pression", "Debit", "Temperature", "Vibration", "Rendement", "Dispo"}) m.var(v, "REAL", "50.0");
    m.cyclic("Pression := 78.0 + 12.0 * SIN(T * 0.6);\nDebit := 70.0 + 15.0 * SIN(T * 0.5 + 1.0);\n"
             "Temperature := 55.0 + 20.0 * SIN(T * 0.4 + 2.0);\nVibration := 30.0 + 25.0 * SIN(T * 0.7 + 3.0);\n"
             "Rendement := 82.0 + 8.0 * SIN(T * 0.3 + 4.0);\nDispo := 88.0 + 6.0 * SIN(T * 0.8 + 5.0);");
    auto& r = m.add(Kind::RadarChart, "Radar", 10, 10, 360, 280);
    r.set("text", "Sant\xC3\xA9 de la pompe P-101 (%)");
    r.set("variables", "Pression; Debit; Temperature; Vibration; Rendement; Dispo");
    r.set("names", "Pression; D\xC3\xA9" "bit; Temp\xC3\xA9rature; Vibration; Rendement; Disponibilit\xC3\xA9");
    r.set("references", "80; 75; 60; 30; 85; 90");
    r.set("colors", "#2ECC71");
    m.note("Explication", 385, 20, 205, 260,
           "Un axe par expression, de 0 \xC3\xA0 100 : le polygone vert est la mesure, le pointill\xC3\xA9 jaune la consigne. "
           "Une pointe qui sort (la vibration) se voit d'un coup d'\xC5\x93il.");
    return m.done(14);
}

Example histogram() {
    Maker m;
    m.var("Poids", "REAL", "500.0");
    m.cyclic("Poids := 500.0 + 2.6 * SIN(T * 7.3) + 1.9 * SIN(T * 13.7 + 1.0) + 1.1 * SIN(T * 29.1 + 2.0) + 0.6 * SIN(T * 51.3);");
    auto& h = m.add(Kind::Histogram, "Pesees", 10, 10, 420, 280);
    h.set("text", "Poids des sachets (g)");
    h.set("variable", "Poids");
    h.setNumber("min", 492);
    h.setNumber("max", 508);
    h.setNumber("bins", 16);
    h.setNumber("samplePeriod", 50);
    h.setNumber("window", 0);
    h.set("low", "495");
    h.set("high", "505");
    m.note("Explication", 440, 20, 150, 260,
           "Chaque mesure tombe dans sa classe. Entre les tol\xC3\xA9rances (495 et 505 g) : la moyenne, l'\xC3\xA9" "cart type, Cp et Cpk ; "
           "les classes dehors sont rouges.");
    return m.done(14);
}

// ---- lot 11 : les alarmes ------------------------------------------------------------------
Example alarmBanner() {
    Maker m;
    m.var("Pression_A", "REAL", "180.0");
    m.var("Porte", "BOOL", "FALSE");
    m.var("Temp", "REAL", "20.0");
    m.cyclic("Pression_A := 180.0 - 18.0 * T;\nPorte := T > 2.0;\nTemp := 20.0 + 6.0 * T;");
    auto& p = m.project();
    addAlarm(p, "Bouteille_Vide_A", "Pression_A < 60.0", "Bouteille A presque vide ({Pression_A:0} bar)", 2, "Armoire A");
    addAlarm(p, "Porte_Ouverte", "Porte", "Porte du local ouverte", 4, "Local");
    addAlarm(p, "Surchauffe", "Temp > 60.0", "Surchauffe armoire A ({Temp:0} \xC2\xB0" "C)", 1, "Armoire A");
    auto& b = m.add(Kind::AlarmBanner, "Bandeau", 10, 20, 580, 48);
    b.setNumber("fontSize", 14);
    m.note("Explication", 20, 90, 560, 200,
           "Le bandeau montre l'alarme la plus grave \xC3\xA0 acquitter, \xC3\xA0 la couleur de sa priorit\xC3\xA9 (elle clignote tant qu'elle "
           "attend). \xC2\xAB +2 \xC2\xBB : les autres (un clic : la suivante). Acquitter l'acquitte ; un clic sur le bandeau la choisit "
           "(sa consigne se montre) et lance ses actions (ouvrir la vue des alarmes).");
    m.step(9.0, "partie", "Bandeau", "suivante");
    m.step(10.5, "partie", "Bandeau", "acquitter");
    return m.done(13);
}

Example alarmCounter() {
    Maker m;
    m.var("Defauts", "INT", "0");
    m.cyclic("Defauts := 0;\nIF T > 1.5 THEN Defauts := 1; END_IF;\nIF T > 3.0 THEN Defauts := 2; END_IF;\n"
             "IF T > 4.5 THEN Defauts := 3; END_IF;\nIF T > 6.0 THEN Defauts := 4; END_IF;");
    auto& p = m.project();
    for (int k = 1; k <= 4; ++k)
        addAlarm(p, "Defaut_" + std::to_string(k), "Defauts >= " + std::to_string(k), "D\xC3\xA9" "faut " + std::to_string(k), k, "Ligne 1");
    auto& a = m.add(Kind::AlarmCounter, "A_Acquitter", 30, 40, 160, 64);
    a.set("label", "\xC3\x80 acquitter");
    auto& b = m.add(Kind::AlarmCounter, "Actives", 220, 40, 160, 64);
    b.set("count", "actives");
    b.set("label", "Actives");
    b.setFlag("blinkUnacked", false);
    auto& c = m.add(Kind::AlarmCounter, "De_Cote", 410, 40, 160, 64);
    c.set("count", "mises de c\xC3\xB4t\xC3\xA9");
    c.set("label", "De c\xC3\xB4t\xC3\xA9");
    auto& shelf = m.add(Kind::Button, "Mettre_De_Cote", 410, 120, 160, 40);
    shelf.set("text", "Mettre de c\xC3\xB4t\xC3\xA9 D1");
    Action act;
    act.trigger = Trigger::Click;
    act.operation = Operation::ShelveAlarm;
    act.target = "Defaut_1";
    act.value = "30; essai du capteur";
    shelf.actions.push_back(act);
    auto& ack = m.add(Kind::Button, "Tout_Acquitter", 30, 120, 160, 40);
    ack.set("text", "Tout acquitter");
    ack.actions.push_back(click(Operation::AckAlarm, "*"));
    m.note("Explication", 30, 180, 540, 110,
           "Un nombre sur la couleur de la priorit\xC3\xA9 la plus forte : \xC3\xA0 acquitter (il clignote), actives, ou mises de c\xC3\xB4t\xC3\xA9. "
           "Un clic peut ouvrir la vue des alarmes (ses actions).");
    m.step(5.5, "clic", "Mettre_De_Cote");
    m.step(9.0, "clic", "Tout_Acquitter");
    return m.done(12);
}

Example alarmSummary() {
    Maker m;
    m.var("Zone_Choisie", "STRING", "''");
    m.var("P1", "REAL", "5.0");
    m.var("P2", "REAL", "5.0");
    m.cyclic("P1 := 5.0 - 0.8 * T;\nP2 := 5.0 - 0.4 * T;");
    auto& p = m.project();
    addAlarm(p, "Pression_Basse_A", "P1 < 2.0", "Pression basse A ({P1:0.0} bar)", 2, "Armoire A");
    addAlarm(p, "Pression_Tres_Basse_A", "P1 < 0.5", "Pression tr\xC3\xA8s basse A", 1, "Armoire A");
    addAlarm(p, "Pression_Basse_B", "P2 < 2.0", "Pression basse B ({P2:0.0} bar)", 3, "Armoire B");
    addAlarm(p, "Porte_Local", "FALSE", "Porte du local", 4, "Local");
    auto& s = m.add(Kind::AlarmSummary, "Resume", 10, 10, 580, 130);
    s.set("variable", "Zone_Choisie");
    auto& h = m.add(Kind::History, "Liste", 10, 150, 580, 110);
    h.setExpr("group", "Zone_Choisie");
    m.title("Zone", 12, 262, 580, "Zone choisie : {Zone_Choisie}", 14);
    m.step(6.5, "partie", "Resume", "zone:0");
    m.step(9.5, "partie", "Resume", "zone:1");
    return m.done(12);
}

Example alarmInstruction() {
    Maker m;
    m.var("Pression_A", "REAL", "180.0");
    m.var("Porte", "BOOL", "FALSE");
    m.cyclic("Pression_A := 180.0 - 30.0 * T;\nPorte := T > 1.0;");
    auto& p = m.project();
    addAlarm(p, "Bouteille_Vide_A", "Pression_A < 60.0", "Bouteille A presque vide ({Pression_A:0} bar)", 2, "Armoire A",
             "1. Ouvrir la bouteille de r\xC3\xA9serve B (vanne V-12).\\n2. Fermer la bouteille A, la d\xC3\xA9poser.\\n"
             "3. Pression restante au moment de l'alarme : {Pression_A:0} bar.");
    addAlarm(p, "Porte_Ouverte", "Porte", "Porte du local ouverte", 4, "Local", "Refermer la porte du local gaz (sas).");
    auto& b = m.add(Kind::AlarmBanner, "Bandeau", 10, 10, 580, 44);
    b.setNumber("fontSize", 13);
    auto& c = m.add(Kind::AlarmInstruction, "Consigne", 10, 66, 580, 224);
    c.setNumber("fontSize", 14);
    m.step(6.0, "clic", "Bandeau");
    m.step(8.5, "partie", "Bandeau", "suivante");
    m.step(9.0, "clic", "Bandeau");
    return m.done(12);
}

Example alarmStats() {
    Maker m;
    m.var("Cycle", "REAL", "0.0");
    m.cyclic("Cycle := T;");
    auto& p = m.project();
    addAlarm(p, "Porte_Ouverte", "SIN(Cycle * 2.4) > 0.6", "Porte ouverte", 4, "Local").ackRequired = false;
    addAlarm(p, "Pression_Basse", "SIN(Cycle * 1.3 + 1.0) > 0.7", "Pression basse", 3, "Armoire A").ackRequired = false;
    addAlarm(p, "Surchauffe", "SIN(Cycle * 0.7 + 2.0) > 0.8", "Surchauffe", 1, "Armoire A").ackRequired = false;
    addAlarm(p, "Niveau_Haut", "SIN(Cycle * 1.9 + 3.0) > 0.85", "Niveau haut", 2, "Cuves").ackRequired = false;
    auto& s = m.add(Kind::AlarmStats, "Statistiques", 10, 10, 580, 190);
    s.setNumber("top", 4);
    m.note("Explication", 20, 210, 560, 80,
           "Les alarmes les plus fr\xC3\xA9quentes depuis le lancement (ou 24 h, 7 jours, tout l'historique) : combien de fois, "
           "combien de temps actives. La barre la plus longue : celle \xC3\xA0 traiter en premier (Pareto).");
    return m.done(16);
}

// ---- lot 11 : la production -------------------------------------------------------------------
Example productionCounter() {
    Maker m;
    m.var("Bons", "DINT", "0");
    m.var("Rebuts", "DINT", "0");
    m.var("Marche", "BOOL", "TRUE");
    m.var("Frac", "REAL", "0.0");
    m.cyclic("Marche := NOT (T > 7.0 AND T < 9.0);\n"
             "IF Marche THEN Frac := Frac + 0.05 * 14.0; END_IF;\n"
             "WHILE Frac >= 1.0 DO Frac := Frac - 1.0; Bons := Bons + 1; IF (Bons MOD 17) = 0 THEN Rebuts := Rebuts + 1; END_IF; END_WHILE;");
    auto& c = m.add(Kind::ProductionCounter, "Ligne_1", 10, 10, 580, 230);
    c.set("text", "Ligne 1");
    c.set("good", "Bons");
    c.set("bad", "Rebuts");
    c.set("running", "Marche");
    c.setNumber("idealRate", 60000);
    c.setNumber("target", 150);
    c.setNumber("rateWindow", 10);
    c.set("shifts", "");
    m.note("Explication", 20, 246, 560, 50,
           "Bonnes, rebuts, cadence et TRS = disponibilit\xC3\xA9 \xC3\x97 performance \xC3\x97 qualit\xC3\xA9 (arr\xC3\xAAt de 7 \xC3\xA0 9 s). RAZ remet le poste \xC3\xA0 z\xC3\xA9ro.");
    m.step(12.0, "partie", "Ligne_1", "raz");
    return m.done(14);
}

Example variableTable() {
    Maker m;
    m.var("Consigne_Temp", "REAL", "60.0");
    m.var("Vitesse", "INT", "1200");
    m.var("Mode", "STRING", "'Auto'");
    m.var("Temp", "REAL", "58.0");
    m.cyclic("Temp := Temp + (Consigne_Temp - Temp) * 0.05;");
    auto& t = m.add(Kind::VariableTable, "Reglages", 10, 10, 400, 170);
    t.set("variables", "Consigne_Temp; Vitesse; Mode; Temp * 1.0");
    t.set("names", "Consigne de temp\xC3\xA9rature; Vitesse du m\xC3\xA9langeur; Mode; Temp\xC3\xA9rature mesur\xC3\xA9" "e");
    t.set("units", "\xC2\xB0" "C; tr/min; ; \xC2\xB0" "C");
    t.set("format", "0.0");
    t.set("keyboard", "num\xC3\xA9rique");
    m.note("Explication", 420, 10, 170, 280,
           "Une ligne par variable, en direct. Un clic sur une valeur (en bleu) la modifie : Entr\xC3\xA9" "e l'\xC3\xA9" "crit (permission Piloter). "
           "Un calcul (la mesure) reste en lecture.");
    m.step(2.0, "partie", "Reglages", "ligne:0");
    m.step(3.0, "clavier", {}, "7");
    m.step(3.5, "clavier", {}, "5");
    m.step(4.2, "clavier", {}, "\xE2\x86\xB5");
    return m.done(12);
}

Example recipeEditor() {
    Maker m;
    m.var("Pression_Consigne", "REAL", "5.0");
    m.var("Debit_Consigne", "REAL", "20.0");
    m.var("Gaz", "STRING", "'Azote'");
    auto& p = m.project();
    Recipe r;
    r.id = p.allocate();
    r.name = "Gaz";
    r.fields = {{"Pression", "Pression_Consigne", "bar", "1", "10"}, {"D\xC3\xA9" "bit", "Debit_Consigne", "l/min", "0", "50"},
                {"Gaz", "Gaz", "", "", ""}};
    RecipeRecord a;
    a.id = p.allocate();
    a.name = "Azote";
    a.values = {"6.5", "25", "'Azote'"};
    RecipeRecord b;
    b.id = p.allocate();
    b.name = "Argon";
    b.values = {"4.0", "18", "'Argon'"};
    r.records = {a, b};
    p.recipes.push_back(r);
    auto& e = m.add(Kind::RecipeEditor, "Editeur", 10, 10, 580, 200);
    e.set("recipe", "Gaz");
    e.set("keyboard", "num\xC3\xA9rique");
    m.note("Explication", 20, 220, 560, 70,
           "Le jeu montr\xC3\xA9, \xC3\xA9l\xC3\xA9ment par \xC3\xA9l\xC3\xA9ment : sa valeur (modifiable, en jaune une fois chang\xC3\xA9" "e), celle de l'installation, "
           "l'\xC3\xA9" "cart. Appliquer \xC3\xA9" "crit les valeurs montr\xC3\xA9" "es ; Enregistrer les garde dans le jeu.");
    m.step(2.0, "partie", "Editeur", "ligne:0");
    m.step(3.0, "clavier", {}, "7");
    m.step(3.4, "clavier", {}, ".");
    m.step(3.8, "clavier", {}, "5");
    m.step(4.4, "clavier", {}, "\xE2\x86\xB5");
    m.step(6.0, "partie", "Editeur", "bouton:Appliquer");
    m.step(8.5, "partie", "Editeur", "bouton:Enregistrer");
    m.step(11.0, "partie", "Editeur", "suivant");
    return m.done(14);
}

Example exportButton() {
    Maker m;
    m.var("Pression_A", "REAL", "180.0");
    m.cyclic("Pression_A := 180.0 - 25.0 * T;");
    auto& p = m.project();
    addAlarm(p, "Bouteille_Vide_A", "Pression_A < 60.0", "Bouteille A presque vide", 2, "Armoire A");
    addAlarm(p, "Bouteille_Vide_B", "Pression_A < 20.0", "Bouteille B presque vide", 3, "Armoire B");
    auto& csv = m.add(Kind::ExportButton, "Export_CSV", 20, 20, 180, 44);
    csv.set("text", "Alarmes");
    csv.set("fileName", "alarmes_{SYS.Date}");
    auto& xl = m.add(Kind::ExportButton, "Export_Excel", 210, 20, 180, 44);
    xl.set("text", "Alarmes");
    xl.set("fileFormat", "Excel");
    xl.set("fileName", "alarmes_{SYS.Date}");
    auto& pdf = m.add(Kind::ExportButton, "Export_PDF", 400, 20, 180, 44);
    pdf.set("text", "Alarmes");
    pdf.set("fileFormat", "PDF");
    pdf.set("fileName", "alarmes_{SYS.Date}");
    m.add(Kind::History, "Liste", 20, 80, 560, 130);
    m.title("Dernier", 20, 222, 560, "Dernier export : {SYS.LastExport}  ({SYS.ExportCount} en tout)", 14);
    // Lot API 8 : en marche, le clic demande ou (l'exemple, lui, n'ecrit rien).
    m.note("Explication", 20, 258, 560, 40, "Un clic demande o\xC3\xB9 \xC3\xA9" "crire (exports/ du projet propos\xC3\xA9) : CSV, Excel (.xlsx) ou PDF.");
    m.step(7.0, "clic", "Export_CSV");
    m.step(9.0, "clic", "Export_Excel");
    m.step(11.0, "clic", "Export_PDF");
    return m.done(13);
}

Example synoptic(Kind k) {
    Maker m;
    m.var("Marche", "BOOL", "FALSE");
    m.var("Defaut", "BOOL", "FALSE");
    m.var("Niveau", "REAL", "20.0");
    m.var("Position", "REAL", "0.0");
    m.var("Mesure", "REAL", "6.0");
    m.cyclic("Marche := T > 1.0 AND T < 8.5;\nDefaut := T > 5.5 AND T < 7.0;\n"
             "Niveau := 20.0 + 70.0 * (0.5 - 0.5 * COS(T * 0.63));\nPosition := 50.0 - 50.0 * COS(T * 1.25);\n"
             "Mesure := 6.0 + 2.5 * SIN(T * 0.9);");
    struct Spec { double w, h; const char* tag; const char* source; const char* shown; const char* note; };
    Spec s{120, 120, "", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}", ""};
    switch (k) {
        case Kind::Valve: s = {150, 120, "V-101", "Marche", "Ouverture {Position:0} %{Defaut: - D\xC3\x89" "FAUT|}",
                               "Tout ou rien : vraie, ouverte (vert) ; fausse, ferm\xC3\xA9" "e. R\xC3\xA9glante : son ouverture (0 \xC3\xA0 100 %) remplit "
                               "la jauge, la tige clignote quand elle bouge. En d\xC3\xA9" "faut, elle clignote."}; break;
        case Kind::Pump: s = {150, 130, "P-101", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}",
                              "En marche, sa roue tourne ; \xC3\xA0 l'arr\xC3\xAAt, elle est grise ; en d\xC3\xA9" "faut, rouge et clignotante."}; break;
        case Kind::Motor: s = {160, 120, "M-12", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}",
                               "Le moteur : un M ; en marche, deux rep\xC3\xA8res tournent autour de sa carcasse."}; break;
        case Kind::Pipe: s = {260, 40, "Ligne N2", "Marche", "{Marche:Le fluide coule|Tube vide}{Defaut: - FUITE|}",
                              "Quand sa valeur est vraie, le fluide coule dans le tube (\xC3\xA0 l'envers si elle est n\xC3\xA9gative). Droit, coude, t\xC3\xA9 ou croix."}; break;
        case Kind::Tank: s = {140, 200, "Cuve T1", "Niveau", "Niveau {Niveau:0} %",
                              "Le contenu monte avec la valeur, entre min et max ; la valeur s'\xC3\xA9" "crit dans la cuve. Les seuils (L, H, HH) "
                              "sont trac\xC3\xA9s ; franchis, leur \xC3\xA9tiquette se remplit."}; break;
        case Kind::GasBottle: s = {70, 210, "Argon", "Niveau * 2.0", "Pression {Niveau*2:0} bar",
                                   "La pression remplit la bouteille (0 \xC3\xA0 200 bar) ; l'ogive prend la couleur normalis\xC3\xA9" "e du gaz."}; break;
        case Kind::Fan: s = {130, 130, "VT-3", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}",
                             "Ses quatre pales tournent en marche."}; break;
        case Kind::Compressor: s = {140, 130, "C-201", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}",
                                    "Le symbole ISO du compresseur ; en marche, un rep\xC3\xA8re tourne sur son cercle."}; break;
        case Kind::HeatExchanger: s = {140, 140, "E-301", "Marche", "{Marche:En service|Hors service}{Defaut: - D\xC3\x89" "FAUT|}",
                                       "En service, le serpentin s'\xC3\xA9" "chauffe (orange) et ondule."}; break;
        case Kind::Filter: s = {110, 130, "F-7", "Marche", "{Marche:En service|Hors service}{Defaut: - COLMAT\xC3\x89|}",
                                "Le filtre : un losange, son m\xC3\xA9" "dia tiret\xC3\xA9 ; colmat\xC3\xA9, il passe en d\xC3\xA9" "faut."}; break;
        case Kind::Boiler: s = {140, 190, "CH-1", "Marche", "{Marche:Br\xC3\xBBleur allum\xC3\xA9|Veilleuse}{Defaut: - D\xC3\x89" "FAUT|}",
                                "Br\xC3\xBBleur allum\xC3\xA9 : la flamme vit ; \xC3\xA9teint : une veilleuse grise."}; break;
        case Kind::Conveyor: s = {300, 70, "Convoyeur 2", "Marche", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - BOURRAGE|}",
                                  "En marche, des chevrons avancent sur la bande (\xC3\xA0 reculons si la valeur est n\xC3\xA9gative)."}; break;
        case Kind::Cylinder: s = {250, 70, "V\xC3\xA9rin V3", "Position", "Tige sortie \xC3\xA0 {Position:0} %",
                                  "La tige suit la valeur, entre min (rentr\xC3\xA9" "e) et max (sortie)."}; break;
        case Kind::IsaInstrument: s = {110, 150, "", "Mesure", "Pression {Mesure:0.0} bar",
                                       "La bulle ISA : ses lettres (PT), sa boucle (101), son montage ; la mesure dessous, rouge au-del\xC3\xA0 du seuil."}; break;
        case Kind::CircuitBreaker: s = {80, 150, "Q1", "Marche", "{Marche:Ferm\xC3\xA9|Ouvert}{Defaut: - D\xC3\x89" "CLENCH\xC3\x89|}",
                                        "Ferm\xC3\xA9 : un carr\xC3\xA9 plein ; ouvert : vide ; d\xC3\xA9" "clench\xC3\xA9 (d\xC3\xA9" "faut) : une croix qui clignote."}; break;
        case Kind::Disconnector: s = {80, 150, "QS2", "Marche", "{Marche:Ferm\xC3\xA9|Ouvert}",
                                      "La lame du sectionneur : ferm\xC3\xA9" "e (verticale) ou ouverte (inclin\xC3\xA9" "e)."}; break;
        case Kind::Contactor: s = {110, 150, "KM1", "Marche", "{Marche:Bobine aliment\xC3\xA9" "e|Au repos}",
                                   "La bobine aliment\xC3\xA9" "e ferme le contact ; au repos, il est ouvert."}; break;
        case Kind::Lamp: s = {100, 100, "H1", "Marche", "{Marche:Allum\xC3\xA9" "e|\xC3\x89teinte}{Defaut: - D\xC3\x89" "FAUT|}",
                              "Allum\xC3\xA9" "e, elle rayonne de sa couleur ; \xC3\xA9teinte, elle est sombre."}; break;
        case Kind::Transformer: s = {90, 160, "T1 20 kV / 400 V", "Marche", "{Marche:Sous tension|Hors tension}",
                                     "Deux enroulements ; sous tension, ils prennent la couleur de marche."}; break;
        case Kind::Silo: s = {130, 210, "Silo S1", "Niveau", "Niveau {Niveau:0} %", "Le grain remplit le silo, jusque dans son c\xC3\xB4ne."}; break;
        case Kind::Hopper: s = {160, 150, "Tr\xC3\xA9mie 3", "Niveau", "Niveau {Niveau:0} %", "La tr\xC3\xA9mie se remplit par le haut, se vide par sa goulotte."}; break;
        case Kind::Mixer: s = {140, 190, "MX-1", "Marche", "{Marche:Agitation|\xC3\x80 l'arr\xC3\xAAt}{Defaut: - D\xC3\x89" "FAUT|}",
                               "Le moteur entra\xC3\xAEne l'arbre ; les pales tournent dans la cuve."}; break;
        case Kind::CheckValve: s = {120, 70, "CL-4", "Marche", "{Marche:Passant|Ferm\xC3\xA9}",
                                    "Le clapet laisse passer dans le sens de la fl\xC3\xA8" "che : vert quand le fluide passe."}; break;
        case Kind::FlowArrow: s = {160, 60, "Vers T1", "Marche", "{Marche:D\xC3\xA9" "bit|Pas de d\xC3\xA9" "bit}",
                                   "Une fl\xC3\xA8" "che de sens d'\xC3\xA9" "coulement ; en marche, une lueur file vers la pointe. Tourner l'objet l'oriente."}; break;
        default: break;
    }
    auto& o = m.add(k, "Symbole", 40 + (240 - std::min(240.0, s.w)) / 2, std::max(10.0, (kHeight - s.h) / 2), s.w, s.h);
    o.set("variable", s.source);
    if (k == Kind::GasBottle) o.setExpr("value", s.source);
    if (*s.tag) o.set("label", s.tag);
    if (k == Kind::IsaInstrument) o.setExpr("fault", "Mesure > 8.2");
    else if (!hmi::kindHasLevel(k)) o.setExpr("fault", "Defaut");
    if (k == Kind::Valve) {
        // Lot 11 : la vanne reglante - son ouverture suit la position, sa tige
        // clignote quand elle bouge.
        o.set("valveType", "r\xC3\xA9glante");
        o.setExpr("opening", "Position");
        o.setExpr("moving", "Marche AND Position > 2.0 AND Position < 98.0");
    }
    if (k == Kind::Tank || k == Kind::Silo || k == Kind::Hopper) {
        // Lot 11 : les seuils dessines sur le contenant.
        o.set("low", "25");
        o.set("high", "80");
        o.set("highAlarm", "88");
    }
    m.title("Etat", 300, 40, 280, s.shown, 18);
    m.note("Explication", 300, 110, 280, 170, s.note);
    return m.done(10);
}


// 1.10.4 : la vanne 3 voies - sa voie active passe de 1-2 a 1-3, puis 2-3, puis
// fermee ; le boisseau bouge entre deux (la tige clignote) ; un defaut a la fin.
Example threeWayValve() {
    Maker m;
    m.var("Voie", "INT", "1");
    m.var("Mouvement", "BOOL", "FALSE");
    m.var("Defaut", "BOOL", "FALSE");
    m.cyclic("Voie := 1;\nIF T >= 2.5 THEN Voie := 2; END_IF;\nIF T >= 4.5 THEN Voie := 3; END_IF;\nIF T >= 6.5 THEN Voie := 0; END_IF;\n"
             "Mouvement := (T > 2.0 AND T < 2.5) OR (T > 4.0 AND T < 4.5) OR (T > 6.0 AND T < 6.5);\n"
             "Defaut := T > 7.5 AND T < 9.0;");
    auto& o = m.add(Kind::ThreeWayValve, "V3V", 70, std::max(10.0, (kHeight - 150.0) / 2), 150, 150);
    o.set("variable", "Voie");
    o.set("label", "V3V-201");
    o.setExpr("fault", "Defaut");
    o.setExpr("moving", "Mouvement");
    m.title("Etat", 300, 40, 280, "Voie active : {Voie:0}{Mouvement: - en mouvement|}{Defaut: - D\xC3\x89" "FAUT|}", 18);
    m.note("Explication", 300, 110, 280, 170,
           "La valeur est la voie active : 1 la voie 1-2, 2 la voie 1-3, 3 la voie 2-3, 0 ferm\xC3\xA9" "e. Les deux voies du passage "
           "sont vertes, le boisseau tourne vers elles ; la tige clignote pendant le mouvement.");
    return m.done(10);
}

// ---- lot 12 : la navigation et la structure --------------------------------------------
// Une vue de l'exemple de navigation : son titre, sa couleur, sa barre.
void navPage(Maker& m, Id view, const std::string& title, const std::string& color, const std::string& text) {
    auto& bar = m.addIn(view, Kind::NavBar, "Barre", 10, 10, 580, 46);
    bar.set("views", "Exemple;Vue_Ligne;Vue_Alarmes");
    bar.set("labels", "Accueil;Ligne 1;Alarmes");
    bar.setFlag("backForward", true);
    bar.set("transition", "Glissement");
    auto& band = m.addIn(view, Kind::Rectangle, "Bande", 10, 70, 580, 8);
    band.set("fill", color);
    band.set("stroke", "");
    auto& t = m.addIn(view, Kind::Text, "Titre", 10, 90, 580, 50);
    t.set("text", title);
    t.setNumber("fontSize", 26);
    t.set("textColor", "#E6EAF0");
    auto& n = m.addIn(view, Kind::Text, "Texte", 10, 150, 580, 110);
    n.set("text", text);
    n.setNumber("fontSize", 14);
    n.set("textColor", "#9AA6B8");
    n.setFlag("wrap", true);
}

Example navBar() {
    Maker m;
    const Id home = m.project().views.front().id;
    const Id line = m.extraView("Vue_Ligne", home);
    const Id alarms = m.extraView("Vue_Alarmes", home);
    navPage(m, home, "Accueil", "#2F6FD6",
            "Une barre de navigation : un bouton par vue, la vue courante en surbrillance. Les fl\xC3\xA8" "ches suivent l'historique.");
    navPage(m, line, "Ligne 1", "#2ECC71", "La vue de la ligne : la barre est la m\xC3\xAAme, son bouton Ligne 1 est allum\xC3\xA9.");
    navPage(m, alarms, "Alarmes", "#E5534B", "Pr\xC3\xA9" "c\xC3\xA9" "dent revient \xC3\xA0 la vue d'avant, comme un navigateur.");
    m.step(1.2, "partie", "Barre", "vue:1");
    m.step(3.2, "partie", "Barre", "vue:2");
    m.step(5.2, "partie", "Barre", "precedent");
    m.step(7.2, "partie", "Barre", "vue:0");
    return m.done(9);
}

Example breadcrumb() {
    Maker m;
    const Id home = m.project().views.front().id;
    const Id prod = m.extraView("Vue_Production", home);
    const Id line = m.extraView("Vue_Ligne_1", prod);
    const auto page = [&](Id view, const std::string& title, const std::string& next, const std::string& text) {
        auto& f = m.addIn(view, Kind::Breadcrumb, "Fil", 10, 10, 580, 38);
        f.set("fill", "#232A34");
        auto& t = m.addIn(view, Kind::Text, "Titre", 10, 64, 580, 46);
        t.set("text", title);
        t.setNumber("fontSize", 24);
        t.set("textColor", "#E6EAF0");
        if (!next.empty()) {
            auto& b = m.addIn(view, Kind::Button, "Entrer", 10, 124, 240, 48);
            b.set("text", "Ouvrir " + viewCaption(next) + " \xE2\x80\xBA");
            Action a;
            a.trigger = Trigger::Click;
            a.operation = Operation::Navigate;
            a.target = next;
            b.actions.push_back(a);
        }
        auto& n = m.addIn(view, Kind::Text, "Texte", 10, 190, 580, 90);
        n.set("text", text);
        n.setNumber("fontSize", 14);
        n.set("textColor", "#9AA6B8");
        n.setFlag("wrap", true);
    };
    page(home, "Accueil", "Vue_Production", "Chaque vue dit sa vue parente (ses propri\xC3\xA9t\xC3\xA9s) : le fil d'Ariane remonte la cha\xC3\xAEne.");
    page(prod, "Production", "Vue_Ligne_1", "Production a Accueil pour parente.");
    page(line, "Ligne 1", "", "Un clic sur Accueil, dans le fil, y retourne directement.");
    m.step(1.2, "clic", "Entrer");
    m.step(3.2, "clic", "Entrer");
    m.step(5.6, "partie", "Fil", "etape:0");
    return m.done(8);
}

Example tabContainer() {
    Maker m;
    m.var("Pression", "REAL", "6.0");
    m.var("Niveau", "REAL", "40.0");
    m.cyclic("Pression := 6.0 + 1.5 * SIN(T * 1.1);\nNiveau := 50.0 + 30.0 * SIN(T * 0.6);");
    Id tid = kNoId;
    {
        auto& tabs = m.add(Kind::TabContainer, "Onglets", 10, 10, 580, 280);
        tabs.set("tabs", "R\xC3\xA9glages;Mesures;Aide");
        tid = tabs.id;
    }
    const auto child = [&](Kind k, const std::string& name, double x, double y, double w, double h, int page) -> Object& {
        auto& o = m.add(k, name, x, y, w, h);
        o.parent = tid;
        o.setNumber("tabPage", page);
        return o;
    };
    auto& t1 = child(Kind::Text, "Consigne", 30, 70, 300, 40, 1);
    t1.set("text", "Consigne de pression : 6,0 bar");
    t1.set("align", "gauche");
    auto& sw = child(Kind::Switch, "Mode_Auto", 30, 130, 180, 60, 1);
    sw.set("variable", "");
    auto& d = child(Kind::NumericDisplay, "Pression_Mesuree", 30, 70, 240, 70, 2);
    d.setExpr("value", "Pression");
    d.set("unit", "bar");
    auto& b = child(Kind::Bargraph, "Niveau_Cuve", 330, 60, 60, 200, 2);
    b.setExpr("value", "Niveau");
    auto& h = child(Kind::Text, "Aide", 30, 70, 530, 120, 3);
    h.set("text", "Trois pages dans la m\xC3\xAAme zone : un clic sur un onglet montre sa page. Chaque objet pos\xC3\xA9 dans le conteneur "
                  "porte sa page.");
    h.setFlag("wrap", true);
    h.set("align", "gauche");
    h.setNumber("fontSize", 15);
    m.step(1.5, "partie", "Onglets", "onglet:2");
    m.step(4.0, "partie", "Onglets", "onglet:3");
    m.step(6.0, "partie", "Onglets", "onglet:1");
    return m.done(8);
}

Example frame() {
    Maker m;
    m.var("Marche", "BOOL", "FALSE");
    m.var("Niveau", "REAL", "40.0");
    m.cyclic("Marche := (REAL_TO_INT(T) MOD 4) < 2;\nNiveau := 50.0 + 35.0 * SIN(T * 0.7);");
    // (Les identifiants d'abord : poser un objet deplace les autres en memoire.)
    Id f1 = kNoId, f2 = kNoId;
    {
        auto& f = m.add(Kind::Frame, "Cadre_Pompe", 10, 20, 280, 250);
        f.set("title", "Pompe P-101");
        f1 = f.id;
    }
    {
        auto& p = m.add(Kind::Pump, "Pompe", 90, 70, 110, 110);
        p.parent = f1;
        p.set("variable", "Marche");
    }
    {
        auto& e = m.add(Kind::Text, "Etat", 30, 200, 240, 40);
        e.parent = f1;
        e.set("text", "{Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}");
    }
    {
        auto& f = m.add(Kind::Frame, "Cadre_Cuve", 310, 20, 280, 250);
        f.set("title", "Cuve T1");
        f.set("titleStyle", "bandeau");
        f2 = f.id;
    }
    {
        auto& t = m.add(Kind::Tank, "Cuve", 390, 70, 120, 180);
        t.parent = f2;
        t.set("variable", "Niveau");
    }
    return m.done(8);
}

Example scrollPanel() {
    Maker m;
    const Id pid = m.add(Kind::ScrollPanel, "Panneau", 10, 10, 360, 280).id;
    const char* lines[] = {"07:40  Mise en service de la ligne", "07:52  Bouteille A branch\xC3\xA9" "e", "08:15  Consigne 6,0 bar",
                           "09:02  Alarme : pression basse A", "09:03  Alarme acquitt\xC3\xA9" "e (chef)", "09:20  Bouteille B en service",
                           "10:44  Recette Azote appliqu\xC3\xA9" "e", "11:30  Arr\xC3\xAAt pour maintenance", "12:10  Red\xC3\xA9marrage",
                           "13:05  Export des alarmes (CSV)"};
    double y = 20;
    int k = 0;
    for (const char* line : lines) {
        auto& row = m.add(Kind::Text, "Ligne_" + std::to_string(++k), 20, y, 320, 40);
        row.parent = pid;
        row.set("text", line);
        row.set("align", "gauche");
        row.setNumber("fontSize", 15);
        row.set("fill", k % 2 ? "#232A34" : "#1B2028");
        y += 46;
    }
    m.note("Explication", 390, 30, 200, 240,
           "Dix lignes dans un cadre de six : la molette ou la barre les fait d\xC3\xA9" "filer. Le panneau rogne ce qui d\xC3\xA9passe.");
    m.step(1.5, "partie", "Panneau", "defiler:92");
    m.step(3.0, "partie", "Panneau", "defiler:92");
    m.step(4.5, "partie", "Panneau", "vbarre:1");
    m.step(6.5, "partie", "Panneau", "vbarre:0");
    return m.done(8);
}

Example collapsiblePanel() {
    Maker m;
    m.var("Pression", "REAL", "6.0");
    m.cyclic("Pression := 6.0 + 1.2 * SIN(T);");
    Id a = kNoId, b = kNoId;
    {
        auto& o = m.add(Kind::CollapsiblePanel, "Panneau_Pompe", 10, 10, 360, 130);
        o.set("title", "Pompe P-101");
        a = o.id;
    }
    {
        auto& t = m.add(Kind::Text, "Pression_Pompe", 30, 60, 320, 36);
        t.parent = a;
        t.set("text", "Pression de refoulement : {Pression:0.0} bar");
        t.set("align", "gauche");
    }
    {
        auto& o = m.add(Kind::CollapsiblePanel, "Panneau_Cuve", 10, 150, 360, 130);
        o.set("title", "Cuve T1");
        b = o.id;
    }
    {
        auto& t2 = m.add(Kind::Text, "Niveau_Cuve", 30, 200, 320, 36);
        t2.parent = b;
        t2.set("text", "Niveau : 64 %  \xC2\xB7  Temp\xC3\xA9rature : 21 \xC2\xB0" "C");
        t2.set("align", "gauche");
    }
    m.note("Explication", 390, 30, 200, 240,
           "Un clic sur le bandeau replie le panneau : le suivant remonte (un accord\xC3\xA9on).");
    m.step(1.5, "partie", "Panneau_Pompe", "entete");
    m.step(3.5, "partie", "Panneau_Cuve", "entete");
    m.step(5.0, "partie", "Panneau_Pompe", "entete");
    m.step(6.5, "partie", "Panneau_Cuve", "entete");
    return m.done(8);
}

std::string plantSvg() {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"580\" height=\"280\" viewBox=\"0 0 580 280\">"
           "<rect width=\"580\" height=\"280\" fill=\"#18202A\"/>"
           "<g fill=\"none\" stroke=\"#3E4C5E\" stroke-width=\"3\">"
           "<rect x=\"20\" y=\"20\" width=\"250\" height=\"110\"/><rect x=\"20\" y=\"150\" width=\"250\" height=\"110\"/>"
           "<rect x=\"300\" y=\"20\" width=\"260\" height=\"240\"/></g>"
           "<g stroke=\"#2B3542\" stroke-width=\"1\">"
           "<line x1=\"40\" y1=\"60\" x2=\"250\" y2=\"60\"/><line x1=\"40\" y1=\"190\" x2=\"250\" y2=\"190\"/>"
           "<line x1=\"330\" y1=\"60\" x2=\"530\" y2=\"60\"/><line x1=\"330\" y1=\"120\" x2=\"530\" y2=\"120\"/></g>"
           "<g fill=\"#2B3542\"><circle cx=\"400\" cy=\"190\" r=\"34\"/><circle cx=\"480\" cy=\"190\" r=\"34\"/></g></svg>";
}

Example zoneMap() {
    Maker m;
    m.svg("plan_site.svg", plantSvg());
    m.var("P_A", "REAL", "6.0");
    m.var("P_B", "REAL", "6.0");
    m.var("Zone_Choisie", "STRING", "''");
    m.cyclic("P_A := 6.0 - 1.4 * T;\nP_B := 6.0 - 0.5 * T;");
    auto& p = m.project();
    addAlarm(p, "Pression_Basse_A", "P_A < 3.0", "Pression basse A ({P_A:0.0} bar)", 2, "Armoire A");
    addAlarm(p, "Pression_Basse_B", "P_B < 3.0", "Pression basse B ({P_B:0.0} bar)", 3, "Armoire B");
    const Id stock = m.extraView("Vue_Stockage", p.views.front().id);
    auto& back = m.addIn(stock, Kind::Button, "Retour", 20, 20, 200, 50);
    back.set("text", "\xE2\x80\xB9 Retour au plan");
    Action a;
    a.trigger = Trigger::Click;
    a.operation = Operation::NavigateBack;
    back.actions.push_back(a);
    auto& t = m.addIn(stock, Kind::Text, "Titre", 20, 100, 560, 60);
    t.set("text", "Stockage : la vue ouverte d'un clic sur sa zone");
    t.setNumber("fontSize", 20);
    auto& map = m.add(Kind::ZoneMap, "Plan", 10, 10, 580, 250);
    map.set("image", "plan_site.svg");
    map.set("variable", "Zone_Choisie");
    map.set("mapZones", "Armoire A | 3.4,7.1 46.6,7.1 46.6,46.4 3.4,46.4 | | | \n"
                        "Armoire B | 3.4,53.6 46.6,53.6 46.6,92.9 3.4,92.9 | | | \n"
                        "Stockage | 51.7,7.1 96.6,7.1 96.6,92.9 51.7,92.9 | | Vue_Stockage | #3A6EA5");
    m.title("Choix", 12, 262, 580, "Zone choisie : {Zone_Choisie}", 14);
    m.step(4.0, "partie", "Plan", "zone:0");
    m.step(6.0, "partie", "Plan", "zone:2");
    m.step(8.0, "clic", "Retour");
    return m.done(10);
}

// ---- lot 12 : le menu natif de connexion ---------------------------------------------
//  Personne n'est connecte : le menu ne montre que Connexion. L'administrateur
//  se connecte ; Mon compte, Comptes, Acces et Journal apparaissent.
Example loginMenuButton() {
    Maker m;
    m.user("operateur", "Paul Martin", kOperateur, "1234");
    m.user("chef", "Claire Dubois", kSuperviseur, "chef");
    m.user("admin", "Alice Admin", kAdministrateur, "admin");
    m.secure("");
    m.add(Kind::LoginMenuButton, "Btn_Connexion", 30, 40, 250, 50);
    m.title("Qui", 300, 44, 290, "{SYS.UserLoggedIn:Connect\xC3\xA9|Personne n'est connect\xC3\xA9} {SYS.UserName} (niveau {SYS.UserLevel})", 15);
    m.title("Onglets", 30, 110, 560, "Onglets : {SYS.LoginMenuTabs}", 14);
    m.note("Explication", 30, 150, 560, 130,
           "Un clic ouvre le menu natif de connexion, sans action \xC3\xA0 \xC3\xA9" "crire. Ses onglets se montrent selon le niveau : "
           "Connexion \xC3\xA0 tout le monde, Mon compte une fois connect\xC3\xA9, Comptes, Acc\xC3\xA8s et Journal aux niveaux "
           "choisis (Configuration > Utilisateurs). Y changer un compte demande la permission Administrer.");
    m.step(0.8, "clic", "Btn_Connexion");
    m.step(1.8, "connexion", "", "suivant");
    m.step(2.4, "connexion", "", "suivant");
    m.step(3.0, "texte", "", "admin");
    m.step(3.8, "connexion", "", "bouton:connexion");
    m.step(5.0, "connexion", "", "onglet:comptes");
    m.step(5.8, "connexion", "", "ligne:2");
    m.step(7.0, "connexion", "", "onglet:acces");
    m.step(8.4, "connexion", "", "onglet:journal");
    m.step(9.8, "connexion", "", "onglet:compte");
    m.step(11.2, "connexion", "", "fermer");
    m.step(12.2, "deconnecter");
    return m.done(13.5);
}

// ---- lot 13 : le selecteur de langue -------------------------------------------------
//  Trois langues, les textes traduits (le texte a trous en entier, le bouton,
//  les etats). English, Deutsch, puis Francais : tout ce qu'on lit change, les
//  valeurs non ; l'explication, sans traduction, reste en francais.
Example languageSelector() {
    Maker m;
    auto& l = m.project().languages;
    l.list = {{"fr", "Fran\xC3\xA7" "ais"}, {"en", "English"}, {"de", "Deutsch"}};
    const auto tr = [&](const std::string& source, const std::string& en, const std::string& de) {
        l.texts[source]["en"] = en;
        l.texts[source]["de"] = de;
    };
    m.var("Pompe_Marche", "BOOL", "FALSE");
    m.cyclic("Pompe_Marche := (REAL_TO_INT(T) MOD 4) < 2;");
    m.add(Kind::LanguageSelector, "Langues", 30, 24, 440, 44);
    m.title("Titre", 30, 88, 560, "Pompe de gavage : {Pompe_Marche:en marche|\xC3\xA0 l'arr\xC3\xAAt}", 18);
    auto& b = m.add(Kind::Button, "Btn_Arret", 30, 142, 200, 46);
    b.set("text", "Arr\xC3\xAAt d'urgence");
    b.set("fill", "#B03A2E");
    auto& st = m.add(Kind::MultiStateText, "Etat", 250, 142, 320, 46);
    st.setExpr("value", "Pompe_Marche");
    st.set("stateList", "FALSE = Arr\xC3\xAAt\xC3\xA9" "e | #4A5261; TRUE = En service | #2ECC71");
    m.note("Explication", 30, 206, 560, 90,
           "Un clic sur une langue : les textes traduits (Configuration > Langues) s'affichent aussit\xC3\xB4t. "
           "Un texte sans traduction reste dans la langue du projet - comme celui-ci.");
    tr("Pompe de gavage : {Pompe_Marche:en marche|\xC3\xA0 l'arr\xC3\xAAt}", "Feed pump: {Pompe_Marche:running|stopped}",
       "Speisepumpe: {Pompe_Marche:l\xC3\xA4uft|steht}");
    tr("Arr\xC3\xAAt d'urgence", "Emergency stop", "Not-Halt");
    tr("Arr\xC3\xAAt\xC3\xA9" "e", "Stopped", "Gestoppt");
    tr("En service", "In service", "In Betrieb");
    m.step(1.5, "partie", "Langues", "langue:1");
    m.step(4.5, "partie", "Langues", "langue:2");
    m.step(7.5, "partie", "Langues", "langue:0");
    return m.done(10);
}

// ---- lot 13 : le selecteur de theme --------------------------------------------------------
//  La nuit (les couleurs de la conception), puis le jour : les fonds sombres
//  deviennent clairs, les textes sombres ; le bouton bleu et les voyants gardent
//  leur couleur franche.
Example themeSelector() {
    Maker m;
    m.var("Pression", "REAL", "3.2");
    m.var("Pompe_Marche", "BOOL", "TRUE");
    m.cyclic("Pression := 3.2 + 0.4 * SIN(T * 0.9);");
    m.add(Kind::ThemeSelector, "Theme", 30, 24, 260, 44);
    m.title("Titre", 30, 88, 560, "Poste de d\xC3\xA9tente : {Pression:0.00} bar", 18);
    auto& g = m.add(Kind::Gauge, "Jauge", 30, 140, 170, 150);
    g.setExpr("value", "Pression");
    g.setNumber("max", 6);
    g.set("unit", "bar");
    auto& v = m.add(Kind::Indicator, "Voyant", 240, 150, 44, 44);
    v.setExpr("value", "Pompe_Marche");
    auto& b = m.add(Kind::Button, "Btn_Marche", 320, 150, 180, 46);
    b.set("text", "Marche");
    b.set("fill", "#2F6FD6");
    m.note("Explication", 240, 214, 340, 80,
           "Jour : les fonds et les textes s'inversent, les couleurs franches restent - pour un \xC3\xA9" "cran en plein soleil.");
    m.step(2.0, "partie", "Theme", "theme:jour");
    m.step(6.0, "partie", "Theme", "theme:nuit");
    return m.done(9);
}

// ---- lot 14 : l'etat de la communication -----------------------------------------------------
//  Le voyant et la ligne : ici le simulateur (bleu) ; relie a un automate reel
//  (Configuration > Communication : Modbus TCP), vert quand il repond, orange
//  pendant la connexion ou quand des valeurs sont anciennes, rouge coupe.
Example commStatus() {
    Maker m;
    m.var("Pression", "REAL", "3.2");
    m.cyclic("Pression := 3.2 + 0.4 * SIN(T * 0.9);");
    m.add(Kind::CommStatus, "Etat_Liaison", 30, 24, 560, 40);
    auto& compact = m.add(Kind::CommStatus, "Etat_Compact", 30, 80, 200, 36);
    compact.setFlag("compact", true);
    m.title("Titre", 30, 136, 560, "Pression : {Pression:0.00} bar", 18);
    m.note("Explication", 30, 180, 560, 96,
           "Vert : l'automate r\xC3\xA9pond (l'adresse, le temps de r\xC3\xA9ponse). Orange : connexion, valeurs anciennes. "
           "Rouge : injoignable (le prochain essai). Bleu : le simulateur de l'application.");
    return m.done(6);
}

// ---- lot 14 : le diagnostic automate ---------------------------------------------------------
//  La liaison en detail. Sur le simulateur : le simulateur, le serveur de
//  demonstration ; relie en Modbus TCP : l'adresse, l'etat et depuis quand,
//  l'identification, les temps de reponse, les erreurs, la qualite des variables
//  (et celles en defaut), Reconnecter et Remettre a zero.
Example plcDiagnostic() {
    Maker m;
    m.add(Kind::PlcDiagnostic, "Diagnostic", 30, 20, 600, 330);
    m.note("Explication", 30, 360, 600, 60,
           "Reconnecter : la liaison se refait tout de suite. Remettre \xC3\xA0 z\xC3\xA9ro : les compteurs de requ\xC3\xAAtes et d'erreurs.");
    return m.done(6);
}

} // namespace

std::optional<Example> exampleFor(Kind kind) {
    switch (kind) {
        case Kind::Text:           return text();
        case Kind::Image:          return image();
        case Kind::Button:         return button();
        case Kind::Rectangle:      return rectangle();
        case Kind::Ellipse:        return ellipse();
        case Kind::Line:           return line();
        case Kind::Polygon:        return polygon();
        case Kind::Indicator:      return indicator();
        case Kind::ProgressBar:    return progressBar();
        case Kind::Gauge:          return gauge();
        case Kind::Table:          return table();
        case Kind::History:        return history();
        case Kind::Trend:          return trend();
        case Kind::List:           return list();
        case Kind::Video:          return video();
        case Kind::Container:      return container();
        case Kind::RecipeManager:  return recipeManager();
        case Kind::AnimatedImage:  return animatedImage();
        case Kind::InputField:     return inputField();
        case Kind::LoginPanel:     return loginPanel();
        case Kind::LogoutButton:   return logoutButton();
        case Kind::UserInfo:       return userInfo();
        case Kind::PasswordChange: return passwordChange();
        case Kind::UserManager:    return userManager();
        // lot 9
        case Kind::PushButton:          return pushButton();
        case Kind::Switch:              return switchControl();
        case Kind::IlluminatedButton:   return illuminatedButton();
        case Kind::Selector:            return selector();
        case Kind::Slider:              return slider();
        case Kind::Knob:                return knob();
        case Kind::ComboBox:            return comboBox();
        case Kind::CheckBox:            return checkBox();
        case Kind::RadioGroup:          return radioGroup();
        case Kind::DateTimePicker:      return dateTimePicker();
        case Kind::WeeklySchedule:      return weeklySchedule();
        case Kind::NumericDisplay:      return numericDisplay();
        case Kind::MultiStateIndicator: return multiStateIndicator();
        case Kind::MultiStateText:      return multiStateText();
        case Kind::Bargraph:            return bargraph();
        case Kind::Thermometer:         return thermometer();
        case Kind::Dial:                return dial();
        case Kind::Clock:               return clock();
        case Kind::HourMeter:           return hourMeter();
        case Kind::SevenSegment:        return sevenSegment();
        case Kind::TrendArrow:          return trendArrow();
        case Kind::Marquee:             return marquee();
        case Kind::QrCode:              return qrCode();
        case Kind::SystemButton:        return systemButton();      // lot 10
        case Kind::Valve:
        case Kind::Pump:
        case Kind::Motor:
        case Kind::Pipe:
        case Kind::Tank:
        case Kind::GasBottle:
        case Kind::Fan:
        case Kind::Compressor:
        case Kind::HeatExchanger:
        case Kind::Filter:
        case Kind::Boiler:
        case Kind::Conveyor:
        case Kind::Cylinder:
        case Kind::IsaInstrument:
        case Kind::CircuitBreaker:
        case Kind::Disconnector:
        case Kind::Contactor:
        case Kind::Lamp:
        case Kind::Transformer:
        case Kind::Silo:
        case Kind::Hopper:
        case Kind::Mixer:
        case Kind::CheckValve:
        case Kind::FlowArrow:           return synoptic(kind);
        case Kind::ThreeWayValve:       return threeWayValve();     // 1.10.4
        case Kind::SymbolInstance:      return symbolInstance();     // lot 10
        // lot 11
        case Kind::BarChart:            return barChart();
        case Kind::XYChart:             return xyChart();
        case Kind::StateChart:          return stateChart();
        case Kind::PieChart:            return pieChart();
        case Kind::RadarChart:          return radarChart();
        case Kind::Histogram:           return histogram();
        case Kind::AlarmBanner:         return alarmBanner();
        case Kind::AlarmCounter:        return alarmCounter();
        case Kind::AlarmSummary:        return alarmSummary();
        case Kind::AlarmInstruction:    return alarmInstruction();
        case Kind::AlarmStats:          return alarmStats();
        case Kind::ProductionCounter:   return productionCounter();
        case Kind::VariableTable:       return variableTable();
        case Kind::RecipeEditor:        return recipeEditor();
        case Kind::ExportButton:        return exportButton();
        // lot 12
        case Kind::NavBar:              return navBar();
        case Kind::Breadcrumb:          return breadcrumb();
        case Kind::TabContainer:        return tabContainer();
        case Kind::Frame:               return frame();
        case Kind::ScrollPanel:         return scrollPanel();
        case Kind::CollapsiblePanel:    return collapsiblePanel();
        case Kind::ZoneMap:             return zoneMap();
        case Kind::LoginMenuButton:     return loginMenuButton();
        // lot 13
        case Kind::LanguageSelector:    return languageSelector();
        case Kind::ThemeSelector:       return themeSelector();
        // lot 14
        case Kind::CommStatus:          return commStatus();
        case Kind::PlcDiagnostic:       return plcDiagnostic();
        case Kind::AnimatedGif:         return animatedGif();          // lot 16
        case Kind::Group:          return std::nullopt;
    }
    return std::nullopt;
}

// ---- lot 16 : le tutoriel ------------------------------------------------------------
namespace {

std::string quoted(const std::string& name) { return "\xC2\xAB " + name + " \xC2\xBB"; }

// "bouton:Appliquer" -> "le bouton Appliquer" ; "ligne:1" -> "la ligne 2"...
std::string partLabel(const std::string& part) {
    const auto colon = part.find(':');
    const std::string head = part.substr(0, colon), rest = colon == std::string::npos ? std::string{} : part.substr(colon + 1);
    const auto number = [&](const char* word) {
        char* end = nullptr;
        const long n = std::strtol(rest.c_str(), &end, 10);
        return std::string(word) + " " + (end && *end == 0 && !rest.empty() ? std::to_string(n + 1) : rest);
    };
    if (head == "bouton") return rest.empty() ? std::string("le bouton") : "le bouton " + rest;
    if (head == "ligne") return number("la ligne");
    if (head == "zone") return number("la zone");
    if (head == "onglet") return rest.empty() ? std::string("un onglet") : "l'onglet " + rest;
    if (head == "champ") return rest.empty() ? std::string("le champ") : rest == "motdepasse" ? std::string("le champ du mot de passe") : "le champ " + rest;
    if (head == "suivant") return "la suivante";
    if (head == "precedent") return "la pr\xC3\xA9" "c\xC3\xA9" "dente";
    if (head == "entete") return "son bandeau";
    if (head == "fraction") return "\xC3\xA0 " + std::to_string(static_cast<int>(std::lround(std::atof(rest.c_str()) * 100))) + " %";
    return part;
}

// Ce que fait une action, sans son declencheur ("Clic -> Basculer Marche" -> "Basculer Marche").
std::string actionWords(const Action& a) {
    const std::string d = describeAction(a);
    const auto arrow = d.find("\xE2\x86\x92 ");
    return arrow == std::string::npos ? d : d.substr(arrow + 4);
}

const Object* objectNamed(const Project& p, const std::string& name) {
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (o.name == name) return &o;
    return nullptr;
}

std::string lowerFirst(std::string s) {
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] - 'A' + 'a');
    return s;
}

bool startsYourTurn(std::string_view title) {
    const std::string f = guide::fold(title);
    return f.rfind("a toi", 0) == 0;
}

} // namespace

std::vector<Chapter> tutorialFor(Kind kind, const Example& ex) {
    std::vector<Chapter> out;
    const auto* topic = guide::topicForKind(kindKey(kind));
    const View* v = ex.project.views.empty() ? nullptr : &ex.project.views.front();
    std::string main;
    if (v)
        for (const auto& o : v->objects)
            if (o.kind == kind) { main = o.name; break; }
    const std::string label(kindLabel(kind));
    // ---- ecrit a la main (@tuto)
    if (topic && !topic->tutorial.empty()) {
        for (const auto& t : topic->tutorial)
            out.push_back({std::clamp(t.at, 0.0, ex.period), t.target, t.title, guide::plain(t.text), startsYourTurn(t.title)});
        std::stable_sort(out.begin(), out.end(), [](const Chapter& a, const Chapter& b) { return a.at < b.at; });
        if (out.empty() || !out.back().yourTurn)
            out.push_back({std::max(out.empty() ? 0.0 : out.back().at + 0.5, ex.period - 1.0), {}, "\xC3\x80 toi",
                           "\xC3\x80 toi : l'exemple est \xC3\xA0 toi, le vrai moteur r\xC3\xA9pond \xC3\xA0 tes clics.", true});
        return out;
    }
    // ---- deduit de l'exemple : la presentation
    out.push_back({0.0, main, topic ? topic->title : label,
                   topic ? guide::plain(topic->summary) : label + " : l'exemple tourne sur le vrai moteur de l'IHM.", false});
    // ---- un chapitre par geste (les touches d'un meme clavier, un appui et son relacher, un glisser : un seul)
    std::vector<std::string> touched;
    const auto touch = [&](const std::string& name) {
        if (!name.empty() && std::find(touched.begin(), touched.end(), name) == touched.end()) touched.push_back(name);
    };
    const Step* prev = nullptr;
    for (const auto& s : ex.steps) {
        const bool pointed = s.op == "clic" || s.op == "partie" || s.op == "texte" || s.op == "clavier" || s.op == "appui"
                          || s.op == "relache" || s.op == "tirer" || s.op == "lacher" || s.op == "menu" || s.op == "connexion"
                          || s.op == "deconnecter";
        if (!pointed) continue;
        // La suite du geste d'avant : dans le meme chapitre.
        const bool follows = prev && s.at - prev->at < 1.3
                          && ((s.op == "clavier" && prev->op == "clavier") || (s.op == "texte" && (prev->op == "partie" || prev->op == "clic"))
                              || (s.op == "relache" && prev->op == "appui" && s.object == prev->object)
                              || ((s.op == "tirer" || s.op == "lacher") && (prev->op == "tirer") && s.object == prev->object));
        if (follows && !out.empty() && out.size() > 1) {
            auto& c = out.back();
            if (s.op == "clavier") {
                c.text += " " + s.arg;
                c.title += s.arg;
            }
            else if (s.op == "texte") c.text += " On tape " + quoted(s.arg) + ".";
            else if (s.op == "relache") c.text += " Rel\xC3\xA2" "ch\xC3\xA9 " + formatDuration(s.at - prev->at) + " plus tard.";
            else if (s.op == "lacher") c.text += " L\xC3\xA2" "ch\xC3\xA9" "e " + partLabel("fraction:" + s.arg) + " : la valeur est \xC3\xA9" "crite.";
            prev = &s;
            continue;
        }
        Chapter c;
        c.at = std::max(out.back().at + 0.4, s.at - 0.7);
        c.target = s.object;
        const Object* o = s.object.empty() ? nullptr : objectNamed(ex.project, s.object);
        if (s.op == "clic") {
            touch(s.object);
            c.title = "Clic : " + s.object;
            c.text = "Un clic sur " + quoted(s.object);
            // Une commande a confirmer : le second clic (dans le delai) la fait agir.
            const bool confirms = o && o->text("confirmMode", "aucune") != "aucune" && prev && prev->op == "clic" && prev->object == s.object;
            if (confirms) {
                c.title = "Confirmer : " + s.object;
                c.text = "Le second clic sur " + quoted(s.object) + " confirme";
            }
            if (o && !o->actions.empty()) {
                c.text += " : " + lowerFirst(actionWords(o->actions.front()));
                if (o->actions.size() > 1) c.text += " (et " + std::to_string(o->actions.size() - 1) + " autre(s) action(s))";
            } else if (o && kindWritesVariable(o->kind)) {
                const std::string var = o->text("variable");
                c.text += var.empty() ? std::string(" : il \xC3\xA9" "crit sa variable") : " : il \xC3\xA9" "crit " + var;
            }
            c.text += ".";
        } else if (s.op == "partie") {
            touch(s.object);
            c.title = partLabel(s.arg);
            if (!c.title.empty() && c.title[0] >= 'a' && c.title[0] <= 'z') c.title[0] = static_cast<char>(c.title[0] - 'a' + 'A');
            c.text = "Sur " + quoted(s.object) + ", " + partLabel(s.arg) + ".";
        } else if (s.op == "texte") {
            c.title = "Au clavier";
            c.text = "On tape " + quoted(s.arg) + ".";
            c.target.clear();
        } else if (s.op == "clavier") {
            c.title = "Au clavier : " + s.arg;
            c.text = "Les touches du clavier virtuel : " + s.arg;
            c.target = "#clavier";
        } else if (s.op == "appui") {
            touch(s.object);
            c.title = "Appui tenu : " + s.object;
            c.text = "Un appui tenu sur " + quoted(s.object) + ".";
        } else if (s.op == "relache") {
            c.title = s.object + " rel\xC3\xA2" "ch\xC3\xA9";
            c.text = quoted(s.object) + " rel\xC3\xA2" "ch\xC3\xA9.";
        } else if (s.op == "tirer" || s.op == "lacher") {
            touch(s.object);
            c.title = "Poign\xC3\xA9" "e : " + s.object;
            c.text = "On tire la poign\xC3\xA9" "e de " + quoted(s.object) + " " + partLabel("fraction:" + s.arg) + ".";
        } else if (s.op == "menu") {
            c.title = "Param\xC3\xA8tres syst\xC3\xA8me";
            c.text = "Le menu natif Param\xC3\xA8tres syst\xC3\xA8me : " + partLabel(s.arg) + ".";
            c.target.clear();
        } else if (s.op == "connexion") {
            c.title = "Le menu de connexion";
            c.text = "Le menu de connexion : " + partLabel(s.arg) + ".";
            c.target.clear();
        } else {
            c.title = "D\xC3\xA9" "connexion";
            c.text = "L'utilisateur se d\xC3\xA9" "connecte : l'objet le montre.";
            c.target = main;
        }
        out.push_back(std::move(c));
        prev = &s;
        if (out.size() >= 9) break;                 // au plus 8 gestes : la suite reste dans le dernier
    }
    // ---- un objet qu'on ne touche pas : ce qu'il montre, ses parametres
    if (out.size() == 1) {
        if (topic && !topic->example.empty())
            out.push_back({ex.period * 0.3, main, "En marche", guide::plain(topic->example), false});
        if (topic && !topic->params.empty()) {
            std::string list;
            std::size_t n = 0;
            for (const auto& prm : topic->params) {
                if (n == 4) break;
                list += (n ? ", " : "") + prm.label;
                ++n;
            }
            out.push_back({ex.period * 0.6, main, "Ses param\xC3\xA8tres",
                           "Dans l'inspecteur : " + list + (topic->params.size() > 4 ? "..." : "") + ". Une expression (=...) les pilote en marche.",
                           false});
        }
    }
    // ---- A toi
    std::string hint;
    for (std::size_t i = 0; i < touched.size() && i < 4; ++i) hint += (i ? ", " : "") + quoted(touched[i]);
    Chapter last;
    last.at = std::clamp(std::max(out.back().at + 1.0, ex.period - 1.0), 0.0, ex.period);
    last.title = "\xC3\x80 toi";
    last.text = hint.empty() ? "\xC3\x80 toi : l'exemple continue de tourner sur le vrai moteur ; change ses param\xC3\xA8tres dans une vue, puis "
                               "Simulation, pour le voir faire la m\xC3\xAAme chose."
                             : "\xC3\x80 toi : clique sur " + hint + " - le vrai moteur r\xC3\xA9pond comme en marche.";
    last.yourTurn = true;
    last.target = touched.empty() ? main : touched.front();
    out.push_back(std::move(last));
    return out;
}

std::size_t play(const Example& ex, Runtime& rt, double from, double to, double now) {
    std::size_t played = 0;
    const View* v = ex.project.views.empty() ? nullptr : &ex.project.views.front();
    const auto idOf = [&](const std::string& name) -> Id {
        if (!v) return kNoId;
        // Lot 12 : un exemple a plusieurs vues - l'objet de la vue montree d'abord.
        if (const View* top = rt.composedView(rt.topView()); top && top->id != v->id)
            for (const auto& o : top->objects) if (o.name == name) return o.id;
        for (const auto& o : v->objects) if (o.name == name) return o.id;
        // Lot 10 : un objet d'une instance de symbole ("Carte_2.Bouton") n'existe
        // que dans la vue qui tourne.
        if (const View* shown = rt.composedView(v->id))
            for (const auto& o : shown->objects) if (o.name == name) return o.id;
        return kNoId;
    };
    for (const auto& s : ex.steps) {
        if (!(s.at > from && s.at <= to)) continue;
        ++played;
        if (s.op == "clic") {
            const Id id = idOf(s.object);
            rt.press(id, now);
            rt.release(id, now, true);
        } else if (s.op == "partie") {
            rt.objectPart(idOf(s.object), s.arg, now);
        } else if (s.op == "texte") {
            rt.typeText(s.arg, now);
        } else if (s.op == "clavier") {
            // Ce que fait la touche du clavier virtuel : son texte, ou sa commande.
            const std::string mode = rt.keyboardMode();
            for (const auto& k : keyboardLayout(mode.empty() ? std::string("numerique") : mode, 280, 250, false)) {
                if (k.label != s.arg) continue;
                if (k.command == "entree") rt.typeKey(EditKey::Enter, now);
                else if (k.command == "echap") rt.typeKey(EditKey::Escape, now);
                else if (k.command == "retour") rt.typeKey(EditKey::Backspace, now);
                else if (!k.text.empty()) rt.typeText(k.text, now);
                break;
            }
        } else if (s.op == "deconnecter") {
            rt.logout(now, "exemple");
        } else if (s.op == "appui") {           // lot 9 : appuyer (et tenir)
            rt.press(idOf(s.object), now);
        } else if (s.op == "relache") {         // ... puis relacher sur l'objet
            rt.release(idOf(s.object), now, true);
        } else if (s.op == "tirer" || s.op == "lacher") {   // la poignee d'un curseur, d'un potentiometre
            rt.dragValue(idOf(s.object), std::atof(s.arg.c_str()), s.op == "lacher", now);
        } else if (s.op == "menu") {            // lot 10 : une partie du menu Parametres systeme
            rt.systemPart(s.arg, now);
        } else if (s.op == "connexion") {       // lot 12 : une partie du menu de connexion
            rt.loginPart(s.arg, now);
        }
    }
    return played;
}

} // namespace hmi::examples
