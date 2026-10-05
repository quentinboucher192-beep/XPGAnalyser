#pragma once
// 1.11 (T1, tranche 16) : ce que le verificateur dit d'un "A toi", sans ecran. Lu par
// app/ScriptRunner (tutoriel-verifier, tutoriel-avant), essaye par tests/tutorial_test.cpp.
//
//  - Une condition, telle que la faute ou l'avertissement la nomment :
//        <chemin> <op> <valeur>, lu << ... >> (ligne N)
//  - UN "A TOI" DEJA VRAI AVANT SES GESTES (l'idee de T3, acceptee par le chef) : la ou
//    "A toi" met l'utilisateur (TutorialPlayer::startATry fait seek(etape, 0) : le bac neuf,
//    les etapes d'avant rejouees, aucun geste de l'etape), toutes ses conditions sont deja
//    vraies. Il ne verifie donc rien : le Bravo viendrait sans un geste. Le verificateur le
//    signale ; c'est un avertissement, pas une faute (ni le code ni les etapes justes ne
//    changent).

#include "help/Tutorial.hpp"

#include <optional>
#include <string>

namespace help {

// La valeur lue, pour une ligne du bilan : sans retour a la ligne, 80 octets au plus
// (coupee entre deux caracteres UTF-8, puis "...").
inline std::string shortReadValue(const std::optional<std::string>& v) {
    std::string lu = v ? *v : std::string("(illisible)");
    for (auto& ch : lu)
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    if (lu.size() > 80) {
        std::size_t cut = 77;
        while (cut > 0 && (static_cast<unsigned char>(lu[cut]) & 0xC0) == 0x80) --cut;
        lu.resize(cut);
        lu += "\xE2\x80\xA6";
    }
    return lu;
}

// "<chemin> <op> <valeur>, lu << ... >> (ligne N)" ; sans la valeur lue si withRead est faux.
inline std::string checkText(const TutorialCheck& ck, const std::optional<std::string>& read, bool withRead = true) {
    std::string s = ck.path + " " + ck.op + " " + ck.value;
    if (withRead) s += ", lu \xC2\xAB " + shortReadValue(read) + " \xC2\xBB";
    return s + " (ligne " + std::to_string(ck.line) + ")";
}

// Un "A toi" deja juste sur l'etat lu (celui d'AVANT ses gestes) : ses conditions, nommees et
// jointes par " et " (la valeur lue n'est redite que si l'operateur n'est pas "=") ; vide s'il
// n'est pas juste : une condition fausse ou illisible, une erreur reconnue (@presque), ou
// aucune condition (@verifier).
inline std::string aTryAlreadyTrue(const TutorialATry& aTry, const PathReader& read) {
    if (aTry.checks.empty() || evaluateATry(aTry, read).result != CheckOutcome::Result::Ok) return {};
    std::string s;
    for (const auto& ck : aTry.checks) {
        if (!s.empty()) s += " et ";
        s += checkText(ck, read ? read(ck.path) : std::nullopt, ck.op != "=");
    }
    return s;
}

} // namespace help
