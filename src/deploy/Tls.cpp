// =============================================================================
//  deploy/Tls.cpp - 1.28.0 : TLS 1.3 avec mbedTLS (voir Tls.hpp)
// =============================================================================
#include "Tls.hpp"

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>

namespace deploy::tls {

std::mutex& libraryLock() {
    static std::mutex m;
    return m;
}

bool ready(std::string* why) {
    static std::once_flag once;
    static psa_status_t status = PSA_ERROR_BAD_STATE;
    if (!crypto::init()) {
        if (why) *why = "le hasard du syst\xC3\xA8me est indisponible (libsodium)";
        return false;
    }
    std::call_once(once, [] {
        std::lock_guard<std::mutex> lock(libraryLock());
        status = psa_crypto_init();
    });
    if (status != PSA_SUCCESS) {
        if (why) *why = "mbedTLS ne d\xC3\xA9marre pas (psa_crypto_init : " + std::to_string(static_cast<int>(status)) + ")";
        return false;
    }
    return true;
}

int randomCallback(void*, unsigned char* out, std::size_t size) {
    crypto::randomBytes(out, size);
    return 0;
}

std::string errorText(int code) {
    char buf[160] = {0};
    mbedtls_strerror(code, buf, sizeof buf);
    char head[24];
    std::snprintf(head, sizeof head, "-0x%04X ", static_cast<unsigned>(code < 0 ? -code : code));
    return std::string(head) + buf;
}

} // namespace deploy::tls

namespace deploy {

namespace {

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1000000000LL));
}

// Les entrees-sorties de mbedTLS sur la socket non bloquante : << il faudrait attendre >> rend
// WANT_READ / WANT_WRITE, et l'attente se fait hors du verrou (TlsStream::drive).
int bioSend(void* ctx, const unsigned char* buf, std::size_t len) {
    const long n = static_cast<net::TcpSocket*>(ctx)->sendSome(buf, len);
    if (n > 0) return static_cast<int>(n);
    if (n == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioRecv(void* ctx, unsigned char* buf, std::size_t len) {
    const long n = static_cast<net::TcpSocket*>(ctx)->receiveSome(buf, len);
    if (n > 0) return static_cast<int>(n);
    if (n == 0) return 0;   // fin : l'autre bout a ferme
    if (n == -2) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

// Aucune autorite : tout certificat bien forme passe la poignee de main (TLS 1.3 prouve que le
// pair detient sa cle) ; l'empreinte est comparee ensuite par l'appelant (l'epinglage).
int acceptPinnedLater(void*, mbedtls_x509_crt*, int, uint32_t* flags) {
    *flags = 0;
    return 0;
}

std::string handshakeText(int rc) {
    switch (rc) {
        case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
            return "l'autre bout a refus\xC3\xA9 la poign\xC3\xA9" "e de main TLS (il n'accepte que TLS 1.3 avec un certificat, ou ce n'est pas un agent XPG)";
        case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
        case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
            return "pas de TLS 1.3 commun avec l'autre bout (" + tls::errorText(rc) + ")";
        case MBEDTLS_ERR_SSL_CONN_EOF:
        case MBEDTLS_ERR_NET_CONN_RESET:
        case MBEDTLS_ERR_NET_RECV_FAILED:
        case MBEDTLS_ERR_NET_SEND_FAILED:
            return "connexion ferm\xC3\xA9" "e pendant la poign\xC3\xA9" "e de main TLS";
        case MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE:
            return "l'autre bout n'a pr\xC3\xA9sent\xC3\xA9 aucun certificat";
        default:
            return "poign\xC3\xA9" "e de main TLS impossible (" + tls::errorText(rc) + ")";
    }
}

} // namespace

struct TlsStream::Impl {
    net::TcpSocket sock;
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    mbedtls_x509_crt own;
    mbedtls_pk_context key;
    bool established{false};
    bool closed{false};
    crypto::Digest peer{};
    std::string peerAddr;
    const std::atomic<bool>* cancel{nullptr};

    Impl() {
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_init(&ssl);
        mbedtls_x509_crt_init(&own);
        mbedtls_pk_init(&key);
    }
    ~Impl() {
        shutdown();
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&own);
        mbedtls_pk_free(&key);
    }

    void shutdown() noexcept {
        if (established && !closed && sock.valid()) {
            std::lock_guard<std::mutex> lock(tls::libraryLock());
            (void)mbedtls_ssl_close_notify(&ssl);   // une fois, sans attendre
        }
        closed = true;
        established = false;
        sock.close();
    }

    bool setup(const TlsIdentity& me, bool server, std::string* why) {
        if (!tls::ready(why)) return false;
        if (!me.valid()) {
            if (why) *why = "pas d'identit\xC3\xA9 TLS";
            return false;
        }
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        int rc = mbedtls_x509_crt_parse_der(&own, me.certificateDer().data(), me.certificateDer().size());
        if (rc == 0)
            rc = mbedtls_pk_parse_key(&key, me.privateKeyDer().data(), me.privateKeyDer().size(), nullptr, 0,
                                      tls::randomCallback, nullptr);
        if (rc == 0)
            rc = mbedtls_ssl_config_defaults(&conf, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0) {
            if (why) *why = "TLS : " + tls::errorText(rc);
            return false;
        }
        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&conf, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
        mbedtls_ssl_conf_rng(&conf, tls::randomCallback, nullptr);
        // Le certificat du pair est exige des deux cotes. mbedTLS veut une << autorite >> pour
        // l'exiger : notre propre certificat tient ce role, et le rappel efface son jugement.
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &own, nullptr);
        if (server) mbedtls_ssl_conf_cert_req_ca_list(&conf, MBEDTLS_SSL_CERT_REQ_CA_LIST_DISABLED);
        rc = mbedtls_ssl_conf_own_cert(&conf, &own, &key);
        if (rc == 0) rc = mbedtls_ssl_setup(&ssl, &conf);
        if (rc == 0 && !server) rc = mbedtls_ssl_set_hostname(&ssl, nullptr);   // pas de nom : l'empreinte suffit
        if (rc != 0) {
            if (why) *why = "TLS : " + tls::errorText(rc);
            return false;
        }
        mbedtls_ssl_set_verify(&ssl, acceptPinnedLater, nullptr);
        mbedtls_ssl_set_bio(&ssl, &sock, bioSend, bioRecv, nullptr);
        return true;
    }

    // Un pas de mbedTLS sous le verrou, puis l'attente (hors du verrou) tant qu'il le demande.
    // Rend le code de mbedTLS (>= 0 : reussi), ou kTimeout / kCancelled.
    static constexpr int kTimeout = -0x7FFF0001;
    static constexpr int kCancelled = -0x7FFF0002;
    int drive(const std::function<int()>& step, Clock::time_point deadline) {
        for (;;) {
            int rc;
            {
                std::lock_guard<std::mutex> lock(tls::libraryLock());
                rc = step();
            }
            if (rc >= 0) return rc;
            if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;   // sans usage ici
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) return rc;
            for (;;) {
                if (cancel && cancel->load()) return kCancelled;
                const int left = remainingMs(deadline);
                if (left <= 0) return kTimeout;
                const int w = sock.wait(rc == MBEDTLS_ERR_SSL_WANT_WRITE, std::min(left, 100));
                if (w > 0) break;
                if (w < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
            }
        }
    }

    bool handshake(int timeoutMs, std::string* why) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
        const int rc = drive([this] { return mbedtls_ssl_handshake(&ssl); }, deadline);
        if (rc == kTimeout) {
            if (why) *why = "pas de poign\xC3\xA9" "e de main TLS en " + std::to_string(timeoutMs) + " ms";
            sock.close();
            return false;
        }
        if (rc == kCancelled) {
            if (why) *why = "interrompu";
            sock.close();
            return false;
        }
        if (rc != 0) {
            if (why) *why = handshakeText(rc);
            sock.close();
            return false;
        }
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        if (mbedtls_ssl_get_version_number(&ssl) != MBEDTLS_SSL_VERSION_TLS1_3) {
            if (why) *why = "TLS 1.3 seulement";
            sock.close();
            return false;
        }
        const mbedtls_x509_crt* pc = mbedtls_ssl_get_peer_cert(&ssl);
        if (!pc || !pc->raw.p || pc->raw.len == 0) {
            if (why) *why = "l'autre bout n'a pr\xC3\xA9sent\xC3\xA9 aucun certificat";
            sock.close();
            return false;
        }
        peer = crypto::sha256(pc->raw.p, pc->raw.len);
        established = true;
        return true;
    }

    Io finish(int rc, std::string* why, const char* what, int timeoutMs) {
        if (rc == kTimeout) {
            if (why) *why = std::string(what) + " : rien en " + std::to_string(timeoutMs) + " ms";
            return Io::Timeout;
        }
        if (rc == kCancelled) {
            if (why) *why = "interrompu";
            return Io::Cancelled;
        }
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF
            || rc == MBEDTLS_ERR_NET_CONN_RESET || rc == MBEDTLS_ERR_NET_RECV_FAILED || rc == MBEDTLS_ERR_NET_SEND_FAILED) {
            if (why) *why = "connexion ferm\xC3\xA9" "e par l'autre bout";
            return Io::Closed;
        }
        if (why) *why = std::string(what) + " : " + tls::errorText(rc);
        return Io::Error;
    }
};

TlsStream::TlsStream() = default;
TlsStream::~TlsStream() = default;
TlsStream::TlsStream(TlsStream&&) noexcept = default;
TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;

bool TlsStream::connect(const std::string& host, int port, const TlsIdentity& me, int timeoutMs, std::string* why) {
    const std::atomic<bool>* cancel = d_ ? d_->cancel : nullptr;
    d_ = std::make_unique<Impl>();
    d_->cancel = cancel;
    const auto start = Clock::now();
    if (!d_->setup(me, false, why)) return false;
    if (!d_->sock.connect(host, port, timeoutMs, why)) return false;
    d_->peerAddr = d_->sock.peerAddress();
    const int used = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    return d_->handshake(std::max(1000, timeoutMs - used), why);
}

bool TlsStream::accept(net::TcpSocket&& socket, const TlsIdentity& me, int timeoutMs, std::string* why) {
    const std::atomic<bool>* cancel = d_ ? d_->cancel : nullptr;
    d_ = std::make_unique<Impl>();
    d_->cancel = cancel;
    d_->sock = std::move(socket);
    d_->peerAddr = d_->sock.peerAddress();
    if (!d_->setup(me, true, why)) return false;
    return d_->handshake(timeoutMs, why);
}

TlsStream::Io TlsStream::write(const void* data, std::size_t size, int timeoutMs, std::string* why) {
    if (!open()) {
        if (why) *why = "pas de connexion";
        return Io::Closed;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
    const auto* p = static_cast<const unsigned char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const int rc = d_->drive([&] { return mbedtls_ssl_write(&d_->ssl, p + done, size - done); }, deadline);
        if (rc <= 0) return d_->finish(rc, why, "envoi", timeoutMs);
        done += static_cast<std::size_t>(rc);
    }
    return Io::Ok;
}

TlsStream::Io TlsStream::read(void* data, std::size_t size, int timeoutMs, std::string* why) {
    if (!open()) {
        if (why) *why = "pas de connexion";
        return Io::Closed;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
    auto* p = static_cast<unsigned char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const int rc = d_->drive([&] { return mbedtls_ssl_read(&d_->ssl, p + done, size - done); }, deadline);
        if (rc <= 0) return d_->finish(rc, why, done ? "r\xC3\xA9ponse incompl\xC3\xA8te" : "lecture", timeoutMs);
        done += static_cast<std::size_t>(rc);
    }
    return Io::Ok;
}

void TlsStream::setCancel(const std::atomic<bool>* flag) noexcept {
    if (!d_) d_ = std::make_unique<Impl>();
    d_->cancel = flag;
}

void TlsStream::close() noexcept {
    if (d_) d_->shutdown();
}

bool TlsStream::open() const noexcept { return d_ && d_->established && !d_->closed && d_->sock.valid(); }

const crypto::Digest& TlsStream::peerFingerprint() const noexcept {
    static const crypto::Digest none{};
    return d_ ? d_->peer : none;
}

std::string TlsStream::peerFingerprintHex() const { return crypto::hex(peerFingerprint()); }

std::string TlsStream::peerAddress() const { return d_ ? d_->peerAddr : std::string(); }

std::string TlsStream::protocolName() const {
    if (!d_ || !d_->established) return {};
    std::lock_guard<std::mutex> lock(tls::libraryLock());
    const char* v = mbedtls_ssl_get_version(&d_->ssl);
    return v ? v : "";
}

std::string TlsStream::cipherName() const {
    if (!d_ || !d_->established) return {};
    std::lock_guard<std::mutex> lock(tls::libraryLock());
    const char* c = mbedtls_ssl_get_ciphersuite(&d_->ssl);
    return c ? c : "";
}

} // namespace deploy
