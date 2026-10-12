// =============================================================================
//  tests/hmi_video_test.cpp - 1.12.12 (lot H) : LA VIDEO DECODEE
// -----------------------------------------------------------------------------
//  Sans ecran :
//   - un petit AVI Motion-JPEG fait ici (des images JPEG ecrites par
//     stb_image_write, mises dans un RIFF AVI) : relu (taille, cadence, nombre
//     d'images, codec), chaque image decodee (ses couleurs, a la perte JPEG
//     pres), une image vide (la precedente), un fichier coupe (les images
//     entieres), une image sans tables de Huffman (celles de la norme remises) ;
//   - la ressource (le gestionnaire de ressources : AVI, sa duree, son codec) ;
//   - la source d'images (l'image a une heure, ce qui change quand elle change) ;
//   - ce que la plateforme decode (un MP4 ailleurs que sous Windows : le conseil) ;
//   - LA LECTURE, avec une horloge factice : a l'affichage, en boucle, une fois,
//     pause et reprise, sur condition, une duree inconnue ;
//   - le moteur : la position ecrite dans sa variable, la pause par sa Lecture,
//     le depart sur condition, la fin d'une lecture « une fois » ;
//   - la file des images d'avance de Media Foundation (sans le fil, avec un faux
//     decodeur) : la premiere image a 0,067 s montree a 0 s, 3 images d'avance,
//     le saut en arriere de la boucle, une image d'avant le saut jetee ;
//   - Generer : le conseil pour un MP4, un depart sur condition sans condition.
//
//  VIDEO_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiAssets.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiMedia.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiVideo.hpp"
#include "../src/hmi/HmiVideoQueue.hpp"

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("VIDEO_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

struct Rgb {
    int r, g, b;
};
constexpr int kW = 32, kH = 24;
const Rgb kColors[3] = {{220, 30, 30}, {30, 200, 40}, {40, 60, 210}};

void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}
// Une image JPEG d'une couleur (et un carre blanc en haut a gauche, pour voir le sens).
Bytes jpegOf(Rgb c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kW) * kH * 3);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const bool corner = x < 8 && y < 8;
            auto* p = &px[(static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 3];
            p[0] = static_cast<std::uint8_t>(corner ? 255 : c.r);
            p[1] = static_cast<std::uint8_t>(corner ? 255 : c.g);
            p[2] = static_cast<std::uint8_t>(corner ? 255 : c.b);
        }
    Bytes out;
    stbi_write_jpg_to_func(appendBytes, &out, kW, kH, 3, px.data(), 95);
    return out;
}
// La couleur d'un pixel d'une image RGBA, et sa distance a une couleur attendue.
bool near(const Rgba& img, int x, int y, Rgb want, int tolerance = 24) {
    if (x >= img.width || y >= img.height) return false;
    const auto* p = &img.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4];
    return std::abs(p[0] - want.r) <= tolerance && std::abs(p[1] - want.g) <= tolerance && std::abs(p[2] - want.b) <= tolerance;
}
std::string colorText(const Rgba& img, int x, int y) {
    if (x >= img.width || y >= img.height) return "?";
    const auto* p = &img.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4];
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]);
}

// ---- 1. l'AVI Motion-JPEG ------------------------------------------------------------------
void aviMjpeg() {
    std::printf("-- un AVI Motion-JPEG ecrit ici, relu et decode\n");
    std::vector<Bytes> frames;
    for (const auto& c : kColors) frames.push_back(jpegOf(c));
    check(frames[0].size() > 100 && frames[0][0] == 0xFF && frames[0][1] == 0xD8, "stb_image_write ecrit des images JPEG");
    const Bytes avi = video::writeMjpegAvi(kW, kH, 10.0, frames);
    check(video::isAvi(avi), "le fichier commence par RIFF ... AVI");
    video::AviInfo info;
    std::string why;
    check(video::parseAvi(avi, info, &why), "relu (" + why + ")");
    check(info.width == kW && info.height == kH, "32 x 24 pixels (strf)");
    check(std::fabs(info.fps - 10.0) < 1e-9 && info.frames.size() == 3 && std::fabs(info.seconds() - 0.3) < 1e-9,
          "10 images par seconde (strh), 3 images (la liste movi), 0,3 s");
    check(info.codec == "MJPG" && info.mjpeg(), "le codec : MJPG, du Motion-JPEG");
    for (std::size_t i = 0; i < 3; ++i) {
        Rgba img;
        const bool ok = video::decodeAviFrame(avi, info, i, img, &why);
        check(ok && img.width == kW && img.height == kH && near(img, 20, 16, kColors[i]) && near(img, 2, 2, {255, 255, 255}),
              "l'image " + std::to_string(i + 1) + " : sa couleur (" + colorText(img, 20, 16) + "), le carre blanc en haut a gauche");
    }
    // Une cadence non entiere (29,97 : 29970 / 1000).
    video::AviInfo ntsc;
    check(video::parseAvi(video::writeMjpegAvi(kW, kH, 29.97, frames), ntsc) && std::fabs(ntsc.fps - 29.97) < 1e-9, "29,97 images par seconde");
    // Une image vide (un AVI qui repete la precedente) : la precedente.
    std::vector<Bytes> withEmpty = {frames[0], frames[1], Bytes{}, frames[2]};
    const Bytes repeat = video::writeMjpegAvi(kW, kH, 10.0, withEmpty);
    video::AviInfo rep;
    Rgba same;
    check(video::parseAvi(repeat, rep) && rep.frames.size() == 4 && video::decodeAviFrame(repeat, rep, 2, same) && near(same, 20, 16, kColors[1]),
          "une image vide : la precedente se montre (4 images, la 3e verte)");
    // Un fichier coupe au milieu de la 3e image : les deux premieres restent.
    if (info.frames.size() == 3) {
        Bytes cut(avi.begin(), avi.begin() + static_cast<std::ptrdiff_t>(info.frames[2].first + info.frames[2].second / 2));
        video::AviInfo partial;
        check(video::parseAvi(cut, partial) && partial.frames.size() == 2, "un fichier coupe : les images entieres (2)");
    }
    // Une image sans tables de Huffman (un AVI de camera, format AVI1) : celles de la norme sont remises.
    Bytes bare;
    {
        const Bytes& j = frames[1];
        std::size_t at = 2;
        bare.assign(j.begin(), j.begin() + 2);
        while (at + 4 <= j.size() && j[at] == 0xFF && j[at + 1] != 0xDA) {
            const std::size_t len = (static_cast<std::size_t>(j[at + 2]) << 8) | j[at + 3];
            if (j[at + 1] != 0xC4) bare.insert(bare.end(), j.begin() + static_cast<std::ptrdiff_t>(at), j.begin() + static_cast<std::ptrdiff_t>(at + 2 + len));
            at += 2 + len;
        }
        bare.insert(bare.end(), j.begin() + static_cast<std::ptrdiff_t>(at), j.end());
    }
    Rgba plain, fixed;
    const bool stbAlone = decodeJpeg(bare.data(), bare.size(), plain);
    const bool ours = video::decodeMjpegFrame(bare.data(), bare.size(), fixed, &why);
    const bool aloneWrong = !stbAlone || !near(plain, 20, 16, kColors[1]);
    check(bare.size() + 400 < frames[1].size() && aloneWrong && ours && near(fixed, 20, 16, kColors[1]),
          "une image sans DHT : stb_image seule la lit mal (" + (stbAlone ? colorText(plain, 20, 16) : std::string("refus\xC3\xA9" "e"))
              + "), avec les tables de la norme elle se lit (" + colorText(fixed, 20, 16) + ")");
    // Pas un AVI.
    check(!video::parseAvi(frames[0], info, &why) && !why.empty(), "une image JPEG n'est pas un AVI : " + why);

    // ---- la ressource : le gestionnaire de ressources la lit (AVI, sa duree, son codec)
    const MediaInfo mi = inspectMedia(avi, "film.avi");
    check(mi.ok() && mi.kind == MediaKind::Video && mi.format == "AVI" && mi.width == kW && mi.height == kH && std::fabs(mi.seconds - 0.3) < 1e-9
              && mi.codec == "Motion-JPEG (MJPG)" && mi.images == 3,
          "inspectMedia : une vid\xC3\xA9o AVI, 32 x 24, 0,3 s, Motion-JPEG (" + mi.codec + ")");
    check(formatFromExtension("film.AVI") == "AVI" && formatFromExtension("a.mov") == "MOV" && formatFromExtension("b.wmv") == "WMV"
              && kindOfFormat("AVI") == MediaKind::Video && kindOfFormat("WMV") == MediaKind::Video
              && std::string(acceptedExtensions()).find("AVI") != std::string::npos,
          "les extensions AVI, MOV, WMV : des vid\xC3\xA9os accept\xC3\xA9" "es");

    // ---- la source d'images : l'image a une heure
    auto blob = std::make_shared<const Bytes>(avi);
    auto source = video::openFrameSource(blob, "AVI", &why);
    check(source != nullptr && std::fabs(source->duration() - 0.3) < 1e-9, "la source d'images d'un AVI Motion-JPEG (0,3 s)");
    if (source) {
        std::uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        const Rgba* a = source->frameAt(0.0, s0);
        const bool firstRed = a && near(*a, 20, 16, kColors[0]);
        const Rgba* b = source->frameAt(0.05, s1);
        const bool stillRed = b && near(*b, 20, 16, kColors[0]);
        const Rgba* c = source->frameAt(0.15, s2);
        const bool green = c && near(*c, 20, 16, kColors[1]);
        const Rgba* d = source->frameAt(0.29, s3);
        const bool blue = d && near(*d, 20, 16, kColors[2]);
        check(firstRed && stillRed && green && blue, "l'image a 0 s, 0,05 s (rouge), 0,15 s (verte), 0,29 s (bleue)");
        check(s0 == s1 && s1 != s2 && s2 != s3, "son numero change quand l'image change (une texture a refaire), pas avant");
        check(!source->failed(), "la source n'a pas echoue");
    }
}

// ---- 2. ce que la plateforme decode ------------------------------------------------------------
void plateforme() {
    std::printf("-- ce que la plateforme decode, et le conseil de Generer\n");
    check(video::decodeAdvice("AVI", "Motion-JPEG (MJPG)").empty() && video::decodedHere("AVI", "Motion-JPEG (MJPG)"),
          "un AVI Motion-JPEG : d\xC3\xA9" "cod\xC3\xA9 partout, aucun conseil");
    check(video::decodeAdvice("MP4", "H.264 (avc1)") == "MP4 : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement ; ailleurs, convertir en Motion-JPEG AVI",
          "un MP4 : " + video::decodeAdvice("MP4", "H.264 (avc1)"));
    check(video::decodeAdvice("AVI", "XVID").rfind("AVI (XVID) : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement", 0) == 0,
          "un AVI d'un autre codec : " + video::decodeAdvice("AVI", "XVID"));
#if !defined(_WIN32)
    check(!video::decodedHere("MP4", "H.264 (avc1)") && !video::decodedHere("WMV", ""), "sous Linux, un MP4 et un WMV ne se d\xC3\xA9" "codent pas");
    std::string why;
    auto blob = std::make_shared<const Bytes>(Bytes{0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 0});
    check(video::openFrameSource(blob, "MP4", &why) == nullptr && why.find("Windows seulement") != std::string::npos,
          "pas de source pour un MP4 ici, et la raison : " + why);
#endif
}


// ---- 2b. les conteneurs que lit le gestionnaire de ressources : MOV, WMV -----------------------
void conteneurs() {
    std::printf("-- les conteneurs : MOV (QuickTime), WMV (ASF)\n");
    const auto be32 = [](Bytes& b, std::uint32_t v) {
        for (int k = 3; k >= 0; --k) b.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const auto le = [](Bytes& b, std::uint64_t v, int n) {
        for (int k = 0; k < n; ++k) b.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const auto tag = [](Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); };
    // Un MOV : ftyp « qt », puis moov / mvhd (1 000 par seconde, 2 500 : 2,5 s).
    Bytes mvhd;
    be32(mvhd, 8 + 4 + 16);
    tag(mvhd, "mvhd");
    be32(mvhd, 0);           // version, drapeaux
    be32(mvhd, 0);           // creation
    be32(mvhd, 0);           // modification
    be32(mvhd, 1000);        // echelle
    be32(mvhd, 2500);        // duree
    Bytes moov;
    be32(moov, static_cast<std::uint32_t>(8 + mvhd.size()));
    tag(moov, "moov");
    moov.insert(moov.end(), mvhd.begin(), mvhd.end());
    Bytes mov;
    be32(mov, 20);
    tag(mov, "ftyp");
    tag(mov, "qt  ");
    be32(mov, 0);
    tag(mov, "qt  ");
    mov.insert(mov.end(), moov.begin(), moov.end());
    const MediaInfo m = inspectMedia(mov, "clip.mov");
    check(m.ok() && m.kind == MediaKind::Video && m.format == "MOV" && std::fabs(m.seconds - 2.5) < 1e-9, "un MOV (ftyp qt) : une vid\xC3\xA9o MOV de 2,5 s");
    const MediaInfo old = inspectMedia(moov, "ancien.mov");
    check(old.ok() && old.format == "MOV" && std::fabs(old.seconds - 2.5) < 1e-9, "un vieux MOV sans ftyp (il commence par moov) : lu aussi");
    // Un WMV : l'en-tete ASF, l'objet des proprietes du fichier (sa duree), celui du flux video (sa taille, son codec).
    static const std::uint8_t kHeader[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
    static const std::uint8_t kFileProps[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
    static const std::uint8_t kStreamProps[16] = {0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
    static const std::uint8_t kVideoMedia[16] = {0xC0, 0xEF, 0x19, 0xBC, 0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B};
    Bytes file;
    file.insert(file.end(), kFileProps, kFileProps + 16);
    le(file, 104, 8);
    le(file, 0, 16);                     // l'identifiant du fichier
    le(file, 0, 8);                      // sa taille
    le(file, 0, 8);                      // sa date
    le(file, 0, 8);                      // les paquets
    le(file, 45000000, 8);               // la duree de lecture : 4,5 s (en 100 ns)
    le(file, 45000000, 8);               // la duree d'envoi
    le(file, 1000, 8);                   // le preroll : 1 000 ms
    le(file, 0, 16);                     // drapeaux, paquets, debit
    Bytes stream;
    stream.insert(stream.end(), kStreamProps, kStreamProps + 16);
    le(stream, 24 + 16 + 16 + 8 + 4 + 4 + 2 + 4 + 11 + 40, 8);
    stream.insert(stream.end(), kVideoMedia, kVideoMedia + 16);
    le(stream, 0, 16);                   // la correction d'erreur
    le(stream, 0, 8);                    // le decalage
    le(stream, 11 + 40, 4);              // les donnees du type
    le(stream, 0, 4);
    le(stream, 1, 2);                    // le flux 1
    le(stream, 0, 4);
    le(stream, 640, 4);                  // la largeur codee
    le(stream, 360, 4);                  // la hauteur codee
    le(stream, 0, 1);
    le(stream, 40, 2);
    le(stream, 40, 4);                   // BITMAPINFOHEADER
    le(stream, 640, 4);
    le(stream, 360, 4);
    le(stream, 1, 2);
    le(stream, 24, 2);
    tag(stream, "WMV3");
    le(stream, 0, 20);
    Bytes wmv(kHeader, kHeader + 16);
    le(wmv, 30 + file.size() + stream.size(), 8);
    le(wmv, 2, 4);
    le(wmv, 1, 1);
    le(wmv, 2, 1);
    wmv.insert(wmv.end(), file.begin(), file.end());
    wmv.insert(wmv.end(), stream.begin(), stream.end());
    const MediaInfo w = inspectMedia(wmv, "film.wmv");
    check(w.ok() && w.kind == MediaKind::Video && w.format == "WMV" && std::fabs(w.seconds - 3.5) < 1e-9 && w.width == 640 && w.height == 360
              && w.codec == "WMV3",
          "un WMV (ASF) : 640 x 360, 3,5 s (la duree de lecture moins le preroll), WMV3 (" + w.codec + ")");
    check(video::decodeAdvice(w.format, w.codec).rfind("WMV : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement", 0) == 0, "... d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement");
}

// ---- 3. la lecture, avec une horloge factice ------------------------------------------------------
void lecture() {
    std::printf("-- la lecture : a l'affichage, en boucle, une fois, pause, sur condition (une horloge factice)\n");
    using video::Playback;
    const auto loop = video::settingsOf("\xC3\xA0 l'affichage", "en boucle");
    const auto once = video::settingsOf("\xC3\xA0 l'affichage", "une fois");
    const auto onCond = video::settingsOf("sur condition", "en boucle");
    check(!loop.onCondition && loop.loop && !once.loop && onCond.onCondition && video::settingsOf("", "").loop && !video::settingsOf("", "").onCondition,
          "les reglages : a l'affichage, en boucle (les defauts), une fois, sur condition");
    // A l'affichage, en boucle : 3 s de video.
    Playback pb;
    video::openView(pb, loop, 10.0);
    check(pb.started && pb.position == 0, "a l'affichage : elle part a l'ouverture de la vue");
    video::step(pb, loop, 11.0, true, false, 3.0);
    check(std::fabs(pb.position - 1.0) < 1e-9 && pb.playing, "1 s plus tard : a 1 s, elle joue");
    video::step(pb, loop, 12.0, false, false, 3.0);
    check(std::fabs(pb.position - 1.0) < 1e-9 && !pb.playing && pb.started, "Lecture fausse : pause, sur l'image du moment");
    video::step(pb, loop, 13.5, true, false, 3.0);
    check(std::fabs(pb.position - 2.5) < 1e-9 && pb.playing, "Lecture vraie : elle reprend la ou elle etait (2,5 s)");
    video::step(pb, loop, 14.0, true, false, 3.0);
    check(std::fabs(pb.position - 0.0) < 1e-9 && pb.loops == 1 && !pb.ended && pb.playing, "en boucle : au bout, elle repart du debut (un tour)");
    // Une fois : elle s'arrete sur sa derniere image.
    Playback one;
    video::openView(one, once, 0.0);
    video::step(one, once, 2.0, true, false, 3.0);
    video::step(one, once, 4.0, true, false, 3.0);
    check(one.ended && !one.playing && std::fabs(one.position - 3.0) < 1e-9, "une fois : finie au bout, sur sa derniere image (3 s)");
    video::step(one, once, 9.0, true, false, 3.0);
    check(one.ended && std::fabs(one.position - 3.0) < 1e-9, "... et elle y reste");
    // Sur condition : elle part quand la condition devient vraie, revient a l'image d'attente quand elle tombe.
    Playback cond;
    video::openView(cond, onCond, 0.0);
    video::step(cond, onCond, 1.0, true, false, 3.0);
    check(!cond.started && cond.position == 0, "sur condition, fausse : l'image d'attente");
    video::step(cond, onCond, 2.0, true, true, 3.0);
    check(cond.started && cond.position == 0, "la condition devient vraie : elle part du debut");
    video::step(cond, onCond, 2.5, true, true, 3.0);
    check(std::fabs(cond.position - 0.5) < 1e-9 && cond.playing, "... et avance (0,5 s)");
    video::step(cond, onCond, 3.0, true, false, 3.0);
    check(!cond.started && cond.position == 0 && !cond.playing, "la condition tombe : retour a l'image d'attente");
    video::step(cond, onCond, 4.0, true, true, 3.0);
    check(cond.started && cond.position == 0, "elle revient : la video repart du debut");
    // Une duree inconnue : elle avance sans fin (le decodeur s'arrete sur sa derniere image).
    Playback unknown;
    video::openView(unknown, once, 0.0);
    video::step(unknown, once, 100.0, true, false, 0.0);
    check(std::fabs(unknown.position - 100.0) < 1e-9 && !unknown.ended, "une duree inconnue : elle avance sans finir");
}

// ---- 4. le moteur -------------------------------------------------------------------------------
Variable variable(Project& p, const char* name, const char* type, const char* initial) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    return v;
}
double readReal(Runtime& rt, const char* path) {
    sim::Value v;
    if (!rt.environment().read(path, v)) return -1;
    return v.type() == sim::Type::Real ? v.asReal() : static_cast<double>(v.asInteger());
}

void moteur() {
    std::printf("-- le moteur : la position ecrite, la pause, le depart sur condition, la fin\n");
    std::vector<Bytes> frames;
    for (const auto& c : kColors) frames.push_back(jpegOf(c));
    Project p;
    const Resource res = makeResource(p, "film.avi", std::make_shared<Bytes>(video::writeMjpegAvi(kW, kH, 10.0, frames)), "essai");
    check(res.format == "AVI" && res.kind() == MediaKind::Video && std::fabs(res.seconds - 0.3) < 1e-9 && res.width == kW
              && res.detail.find("Motion-JPEG") != std::string::npos,
          "la ressource : une vid\xC3\xA9o AVI de 0,3 s (" + res.detail + ")");
    p.assets.resources.push_back(res);
    p.programs.variables.push_back(variable(p, "Pos", "REAL", "0"));
    p.programs.variables.push_back(variable(p, "Lecture", "BOOL", "TRUE"));
    p.programs.variables.push_back(variable(p, "Marche", "BOOL", "FALSE"));
    View v = makeView(p, "Vue_Video");
    p.config.cycleMs = 50;
    const auto put = [&](const char* name) -> Object& {
        const Id id = edit::add(p, v, Kind::Video, 10, 10);
        Object& o = *v.object(id);
        o.name = name;
        o.set("video", "film.avi");
        return o;
    };
    Object& a = put("Boucle");
    a.set("positionVariable", "Pos");
    a.set("playing", "Lecture");
    const Id idA = a.id;
    Object& b = put("UneFois");
    b.set("play", "une fois");
    const Id idB = b.id;
    Object& c = put("Condition");
    c.set("start", "sur condition");
    c.set("condition", "Marche");
    const Id idC = c.id;
    Object& old = put("Ancienne");   // une video de la 1.12.9 : ni Lecture, ni depart, ni repetition
    const Id idOld = old.id;
    Object& bad = put("Refusee");    // une variable de position inconnue : dite une fois, pas a chaque cycle
    bad.set("positionVariable", "Pos_Inconnue");
    const Id idBad = bad.id;
    p.config.startView = v.id;
    p.views = {v};
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    const auto* pa = rt.videoPlayback(idA);
    check(pa && pa->started && std::fabs(pa->position - 0.1) < 1e-6, "a l'affichage : partie, a 0,1 s");
    check(std::fabs(readReal(rt, "Pos") - 0.1) < 1e-6, "sa position est ecrite dans Pos (0,1 s)");
    const auto* pOld = rt.videoPlayback(idOld);
    check(pOld && pOld->started && pOld->playing, "une video sans les nouveaux reglages : elle joue a l'affichage, en boucle");
    rt.environment().write("Lecture", sim::Value::boolean(false));
    rt.tick(0.2);
    check(pa && std::fabs(pa->position - 0.1) < 1e-6 && !pa->playing, "Lecture fausse : en pause a 0,1 s");
    rt.environment().write("Lecture", sim::Value::boolean(true));
    rt.tick(0.3);
    rt.tick(0.4);
    check(pa && std::fabs(pa->position - 0.0) < 1e-6 && pa->loops == 1, "Lecture vraie : elle reprend, puis reboucle au bout (0,3 s)");
    const auto* pb = rt.videoPlayback(idB);
    check(pb && pb->ended && std::fabs(pb->position - 0.3) < 1e-6, "une fois : finie, sur sa derniere image");
    bool told = false;
    for (const auto& e : rt.journal()) told = told || (e.message.find("fin de la lecture") != std::string::npos && e.source.find("UneFois") != std::string::npos);
    check(told, "la fin est dite dans la Console");
    const auto* pc = rt.videoPlayback(idC);
    check(pc && !pc->started, "sur condition (Marche faux) : l'image d'attente");
    rt.environment().write("Marche", sim::Value::boolean(true));
    rt.tick(0.5);
    rt.tick(0.6);
    check(pc && pc->started && std::fabs(pc->position - 0.1) < 1e-6, "Marche vrai : elle part du debut et avance (0,1 s)");
    rt.environment().write("Marche", sim::Value::boolean(false));
    rt.tick(0.7);
    check(pc && !pc->started && pc->position == 0, "Marche retombe : retour a l'image d'attente");
    const auto* pBad = rt.videoPlayback(idBad);
    std::size_t refusals = 0;
    for (const auto& e : rt.journal()) refusals += e.kind == "Erreur" && e.source.find("Refusee") != std::string::npos;
    check(pBad && pBad->started && refusals == 1,
          "une variable de position inconnue : la video joue, la Console le dit une fois (" + std::to_string(refusals) + " message(s) en 7 cycles)");

    // ---- Generer : le conseil pour un MP4 (une information), un depart sur condition sans condition
    Resource mp4;
    mp4.id = p.allocate();
    mp4.name = "presentation.mp4";
    mp4.format = "MP4";
    mp4.detail = "H.264 (avc1)";
    mp4.data = std::make_shared<Bytes>(Bytes{0, 0, 0, 8, 'f', 'r', 'e', 'e'});
    p.assets.resources.push_back(mp4);
    View& pv = p.views[0];
    const Id idMp4 = edit::add(p, pv, Kind::Video, 300, 10);
    pv.object(idMp4)->set("video", "presentation.mp4");
    const Id idNoCond = edit::add(p, pv, Kind::Video, 300, 300);
    pv.object(idNoCond)->set("video", "film.avi");
    pv.object(idNoCond)->set("start", "sur condition");
    const auto issues = generate(p, [](std::string_view) { return false; });
    bool advice = false, noCondition = false, aviSilent = true, blocking = false, unknownPos = false;
    for (const auto& i : issues) {
        if (i.object == idMp4 && i.property == "video" && i.severity == Issue::Severity::Info
            && i.message.find("MP4 : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement ; ailleurs, convertir en Motion-JPEG AVI") != std::string::npos)
            advice = true;
        if (i.object == idNoCond && i.property == "condition" && i.severity == Issue::Severity::Warning) noCondition = true;
        if (i.object == idBad && i.property == "positionVariable" && i.severity == Issue::Severity::Warning
            && i.message.find("Pos_Inconnue") != std::string::npos)
            unknownPos = true;
        if (i.object == idA && i.property == "video") aviSilent = false;
        if ((i.object == idA || i.object == idB || i.object == idC || i.object == idOld || i.object == idMp4 || i.object == idNoCond)
            && i.severity == Issue::Severity::Error)
            blocking = true;
    }
    check(advice, "Generer : \xC2\xAB MP4 : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement ; ailleurs, convertir en Motion-JPEG AVI \xC2\xBB (une information)");
    check(noCondition, "Generer : un depart sur condition sans condition (un avertissement)");
    check(unknownPos, "Generer : une variable de position inconnue (un avertissement)");
    check(aviSilent && !blocking, "Generer : rien sur l'AVI Motion-JPEG, et rien de bloquant");
}

// ---- 5. la file des images d'avance (Media Foundation), avec un faux decodeur -----------------------
void fileDavance() {
    std::printf("-- la file des images d'avance de Media Foundation : un faux decodeur, sans fil\n");
    using video::FrameQueue;
    const auto image = [](int shade) {
        Rgba r;
        r.width = 1;
        r.height = 1;
        r.pixels = {static_cast<std::uint8_t>(shade), 0, 0, 255};
        return r;
    };
    const auto shadeOf = [](const Rgba* r) { return r && !r->pixels.empty() ? static_cast<int>(r->pixels[0]) : -1; };
    FrameQueue q;
    std::uint64_t serial = 0;
    check(q.frameAt(0.0, serial) == nullptr && serial == 0 && q.wantsWork(false), "vide : rien a montrer, le fil decode");
    // Un MP4 a images B : sa premiere image est a 0,067 s (2/30).
    int made = 0;
    double t = 2.0 / 30.0;
    const auto decode = [&] {
        q.push(t, image(++made));
        t += 1.0 / 30.0;
    };
    while (q.wantsWork(false) && made < 10) decode();
    check(made == 3 && q.size() == FrameQueue::kAhead && !q.wantsWork(false), "le fil s'arrete a 3 images d'avance");
    check(shadeOf(q.frameAt(0.0, serial)) == 1 && serial == 1,
          "la premiere image (a 0,067 s) se montre a 0 s - l'apercu de l'editeur (avant : rien, l'image d'attente)");
    check(q.wantsWork(false), "... une place libre : le fil reprend");
    check(shadeOf(q.frameAt(0.05, serial)) == 1 && serial == 1, "a 0,05 s : la meme image (la suivante est a 0,1 s), le meme numero");
    check(shadeOf(q.frameAt(0.1, serial)) == 2 && serial == 2, "a 0,1 s : la deuxieme image");
    while (q.wantsWork(false) && made < 10) decode();
    check(shadeOf(q.frameAt(0.25, serial)) == 5 && serial == 5 && q.size() == 0, "en retard (0,25 s) : les images passees defilent d'un coup, la 5e");
    check(shadeOf(q.frameAt(0.3, serial)) == 5 && shadeOf(q.frameAt(0.2, serial)) == 5 && q.takeSeek() < 0,
          "un recul de moins d'un quart de seconde : pas de saut");
    // La boucle : retour au debut.
    (void)q.frameAt(0.5, serial);
    check(shadeOf(q.frameAt(0.0, serial)) == 5 && serial == 5 && q.wantsWork(true),
          "la boucle (0,5 s puis 0 s) : la derniere image reste, le fil doit repartir (meme fini)");
    q.push(0.5333, image(99));   // decodee pendant le saut : d'avant lui
    check(q.size() == 0, "une image decodee d'avant le saut est jetee");
    check(q.takeSeek() == 0.0 && q.takeSeek() < 0, "le fil repart de 0 s (une seule fois)");
    t = 2.0 / 30.0;
    made = 9;
    decode();
    check(shadeOf(q.frameAt(0.02, serial)) == 5 && serial == 5, "a 0,02 s : encore la derniere image (la premiere est a 0,067 s)");
    check(shadeOf(q.frameAt(0.07, serial)) == 10 && serial == 6, "a 0,07 s : la premiere image du nouveau tour");
}

} // namespace

int main() {
    aviMjpeg();
    plateforme();
    conteneurs();
    lecture();
    moteur();
    fileDavance();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
