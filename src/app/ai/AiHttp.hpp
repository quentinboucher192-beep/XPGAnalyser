// =============================================================================
//  app/ai/AiHttp.hpp - l'Assistant IA : HTTPS (le CAIHttpClient de la demande)
// -----------------------------------------------------------------------------
//  UNE REQUETE A LA FOIS, BLOQUANTE, SUR UN FIL DE TRAVAIL (jamais celui de
//  l'interface) : POST ou GET, des en-tetes, un corps ; la reponse arrive par
//  morceaux (un rappel par morceau, pour le flux SSE) ; deux delais (la
//  connexion, et l'inactivite : rien recu pendant N secondes) ; ARRETER depuis
//  un autre fil (cancel) ferme la connexion, perform rend aussitot.
//
//  LE TRANSPORT DU SYSTEME, sans rien livrer a cote de l'exe :
//    - Windows : WinHTTP (winhttp.dll, livree avec Windows : le TLS, les proxys
//      et les certificats du poste et de l'entreprise) ;
//    - Linux   : libcurl, CHARGEE A LA DEMANDE (dlopen de libcurl.so.4 : aucune
//      dependance a la compilation) ; absente, l'Assistant le dit.
//  http:// n'est accepte que vers CE poste (127.0.0.1, localhost, ::1 : les
//  essais, un relais local) : la cle API ne passe jamais en clair sur le reseau.
//
//  LA FABRIQUE EST INJECTABLE (setHttpFactory) : un essai y met un faux
//  transport qui rejoue des reponses enregistrees, sans reseau.
//
//  Rien ici n'ecrit un en-tete dans un message ni dans un journal : la cle API
//  (x-api-key) ne sort jamais de la requete.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// Une adresse lue : https://hote:port/chemin?requete
struct Url {
    bool        https{true};
    std::string host;           // sans les crochets d'une adresse IPv6
    int         port{443};
    std::string path{"/"};      // le chemin et la requete, "/" au moins
    bool        hasPath{false}; // l'adresse ecrite avait un chemin (autre que "/")
    [[nodiscard]] bool        loopback() const;   // 127.x.x.x, localhost, ::1
    // 1.16.0 : une adresse privee du reseau local (10.x, 192.168.x, 172.16-31.x) - un serveur Ollama de l'usine.
    [[nodiscard]] bool        privateLan() const;
    [[nodiscard]] std::string text() const;       // l'adresse recomposee
    [[nodiscard]] std::string base() const;       // https://hote[:port], sans chemin
};
// Faux et `why` (en francais) : pas une adresse http(s), un hote vide, un port faux, ou
// http:// vers un autre poste que celui-ci.
bool parseUrl(std::string_view text, Url& out, std::string* why = nullptr);
// 1.16.0 : `allowPrivateHttp` accepte aussi http:// vers une adresse privee du reseau local (un serveur
// sans cle : la requete ne porte aucun secret) ; l'appelant le demande seulement sans cle.
bool parseUrl(std::string_view text, Url& out, std::string* why, bool allowPrivateHttp);

struct HttpRequest {
    std::string method{"POST"};           // "POST" | "GET"
    std::string url;
    HttpHeaders headers;                  // en plus : rien (pas d'Expect, pas de compression)
    std::string body;
    int         connectTimeoutSeconds{20};
    int         idleTimeoutSeconds{60};   // rien recu pendant ce delai : Timeout
    // 1.16.0 (a la fin) : http:// vers une adresse privee du reseau local est accepte (une requete
    // SANS cle, vers un serveur de l'usine : parseUrl(..., true)).
    bool        allowPrivateHttp{false};
};

struct HttpResponse {
    enum class Failure : std::uint8_t {
        None,         // la reponse est arrivee en entier (quel que soit son code)
        Unavailable,  // pas de transport sur ce poste (libcurl manque)
        BadUrl,       // l'adresse est refusee (parseUrl)
        Resolve,      // le nom de l'hote est inconnu (hors ligne ?)
        Connect,      // la connexion est impossible (hors ligne, pare-feu, proxy)
        Tls,          // la connexion securisee est refusee (certificat)
        Timeout,      // le delai de connexion ou d'inactivite est depasse
        Cut,          // la connexion s'est coupee pendant la reponse
        Cancelled,    // Arreter (cancel)
        Other
    };
    Failure     failure{Failure::None};
    std::string error;        // en francais, pour l'utilisateur (vide : la reponse est complete)
    int         status{0};    // le code HTTP (0 : aucune reponse)
    HttpHeaders headers;      // les cles en minuscules
    [[nodiscard]] std::string header(std::string_view name) const;
    [[nodiscard]] bool        ok() const noexcept { return failure == Failure::None; }
};

// Ce qui recoit la reponse, sur le fil de travail, dans l'ordre : les en-tetes (une fois,
// quand le code final est connu), puis les morceaux du corps.
struct HttpSink {
    std::function<void(int status, const HttpHeaders& headers)> onHeaders;
    std::function<void(std::string_view chunk)>                  onData;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    // Bloquant (un fil de travail) : rend quand la reponse est finie, coupee ou annulee.
    virtual HttpResponse perform(const HttpRequest&, const HttpSink&) = 0;
    // Depuis n'importe quel fil : la connexion se ferme, perform rend Cancelled. Definitif :
    // une requete suivante sur ce client rend Cancelled aussitot.
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool cancelled() const noexcept = 0;
};

using HttpFactory = std::function<std::unique_ptr<HttpClient>()>;
// Un essai y met son faux transport ; nul : celui du systeme.
void setHttpFactory(HttpFactory factory);
// Un client neuf (celui de la fabrique injectee, sinon WinHTTP ou libcurl).
[[nodiscard]] std::unique_ptr<HttpClient> makeHttpClient();
// Le transport du systeme est-il la ? (Windows : toujours ; Linux : libcurl trouvee.) Faux et
// `why` : "pas de connexion possible sur ce poste (libcurl manque)". Une fabrique injectee : vrai.
[[nodiscard]] bool httpAvailable(std::string* why = nullptr);
// Le nom du transport ("WinHTTP", "libcurl 8.5.0") ; "essai" pour une fabrique injectee.
[[nodiscard]] std::string httpTransportName();

// Le message d'une panne, en francais (Failure -> texte) ; `host` le nomme quand il sert.
[[nodiscard]] std::string failureText(HttpResponse::Failure, std::string_view host, int idleSeconds = 0);

// Le base64 standard, sans retour a la ligne (les images et les PDF de la requete).
[[nodiscard]] std::string base64(std::string_view bytes);

} // namespace app::ai
