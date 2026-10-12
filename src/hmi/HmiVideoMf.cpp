// =============================================================================
//  hmi/HmiVideoMf.cpp - 1.12.12 (lot H) : la video par Media Foundation (Windows)
// -----------------------------------------------------------------------------
//  MP4 / H.264, WMV, MOV, un AVI d'un autre codec que le Motion-JPEG : le
//  lecteur de Windows (IMFSourceReader), en RGB32, avec son traitement video
//  (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING : il convertit le codec en RGB).
//  Un fil a part decode, image par image, sans tout charger : il garde au plus
//  quelques images d'avance sur celle qu'on montre, et repart du debut quand la
//  lecture reboucle (ou d'ou on la demande). La ressource passe par un fichier
//  temporaire (le lecteur reconnait le format a son extension), efface a la
//  fermeture. Les images d'avance passent par une FrameQueue (HmiVideoQueue.hpp :
//  verifiee partout).
//
//  MEDIA FOUNDATION SE CHARGE A LA DEMANDE (mfplat.dll, mfreadwrite.dll, pris
//  dans System32 a la premiere video qui en a besoin) : un Windows qui ne l'a pas
//  - une edition N sans le Media Feature Pack, un Windows Server sans ses
//  fonctions video - demarre quand meme, et la video y montre son image
//  d'attente et le conseil. Liees a l'edition des liens, ces deux DLL manquantes
//  empechaient les deux applications de demarrer. Les bibliotheques d'import
//  restent liees pour leurs GUID (mfuuid) : aucune fonction n'en est importee.
//
//  Ailleurs que sous Windows, rien : openMediaFoundation rend nul (le Motion-JPEG
//  en AVI, lui, se decode partout : HmiVideo.cpp).
// =============================================================================
#include "HmiVideo.hpp"
#include "HmiVideoQueue.hpp"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mfapi.h>
#  include <mferror.h>
#  include <mfidl.h>
#  include <mfreadwrite.h>
#  include <propidl.h>

#  include <algorithm>
#  include <condition_variable>
#  include <cstdio>
#  include <mutex>
#  include <thread>
#endif

namespace hmi::video {

#if defined(_WIN32)
namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring wide(std::string_view s) {
    std::wstring out;
    for (const char c : s) out += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return out;
}

// Un fichier temporaire au contenu de la ressource, a la bonne extension.
std::wstring writeTemp(const Bytes& data, std::string_view format) {
    wchar_t dir[MAX_PATH + 1] = {};
    const DWORD n = GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n > MAX_PATH) return {};
    static long counter = 0;
    std::string ext(format);
    for (auto& c : ext) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (ext.empty()) ext = "mp4";
    const std::wstring path = std::wstring(dir) + L"xpg_video_" + std::to_wstring(GetCurrentProcessId()) + L"_"
                            + std::to_wstring(InterlockedIncrement(&counter)) + L"." + wide(ext);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1u << 24));
        DWORD written = 0;
        ok = WriteFile(f, data.data() + done, chunk, &written, nullptr) && written == chunk;
        done += written;
    }
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(path.c_str());
        return {};
    }
    return path;
}

// Les cinq fonctions de Media Foundation qui servent, chargees une fois pour toutes (les DLL restent chargees).
struct MfApi {
    using Startup = HRESULT(WINAPI*)(ULONG, DWORD);
    using Shutdown = HRESULT(WINAPI*)();
    using CreateAttributes = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
    using CreateMediaType = HRESULT(WINAPI*)(IMFMediaType**);
    using CreateSourceReaderFromURL = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
    Startup                   startup{nullptr};
    Shutdown                  shutdown{nullptr};
    CreateAttributes          createAttributes{nullptr};
    CreateMediaType           createMediaType{nullptr};
    CreateSourceReaderFromURL createSourceReaderFromURL{nullptr};
    std::string               missing;   // vide : tout est la ; sinon ce qui manque (mfplat.dll...)
};

// Une DLL du systeme, par son chemin dans System32 (pas une copie posee a cote de l'exe).
HMODULE systemLibrary(const wchar_t* name) {
    wchar_t dir[MAX_PATH + 1] = {};
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    return LoadLibraryW((std::wstring(dir) + L"\\" + name).c_str());
}

template <class F>
bool load(F& f, HMODULE m, const char* name) {
    f = m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
    return f != nullptr;
}

const MfApi& mfApi() {
    static const MfApi api = [] {
        MfApi a;
        HMODULE plat = systemLibrary(L"mfplat.dll");
        HMODULE reader = plat ? systemLibrary(L"mfreadwrite.dll") : nullptr;
        if (!plat) {
            a.missing = "mfplat.dll";
        } else if (!reader) {
            a.missing = "mfreadwrite.dll";
        } else {
            const bool all = load(a.startup, plat, "MFStartup") && load(a.shutdown, plat, "MFShutdown")
                          && load(a.createAttributes, plat, "MFCreateAttributes") && load(a.createMediaType, plat, "MFCreateMediaType")
                          && load(a.createSourceReaderFromURL, reader, "MFCreateSourceReaderFromURL");
            if (!all) a.missing = "une fonction de mfplat.dll ou de mfreadwrite.dll";
        }
        return a;
    }();
    return api;
}

class MfSource final : public FrameSource {
public:
    MfSource(BlobPtr data, std::wstring path) : data_(std::move(data)), path_(std::move(path)) {
        thread_ = std::thread([this] { run(); });
    }
    ~MfSource() override {
        {
            const std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
        DeleteFileW(path_.c_str());
    }
    const Rgba* frameAt(double seconds, std::uint64_t& serial) override {
        const std::lock_guard<std::mutex> lock(m_);
        // Revenue en arriere (la boucle, un nouveau depart) : la file le demande au fil. L'image rendue n'est
        // touchee que par frameAt (le dessin) : elle reste valable hors du verrou.
        const Rgba* image = queue_.frameAt(seconds, serial);
        cv_.notify_all();
        return image;
    }
    bool failed(std::string* error) const override {
        const std::lock_guard<std::mutex> lock(m_);
        if (failed_ && error) *error = error_;
        return failed_;
    }
    double duration() const override {
        const std::lock_guard<std::mutex> lock(m_);
        return duration_;
    }

private:
    void fail(std::string why) {
        const std::lock_guard<std::mutex> lock(m_);
        failed_ = true;
        error_ = std::move(why);
    }

    void run() {
        const MfApi& mf = mfApi();   // (openMediaFoundation l'a verifie : tout y est)
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(mf.startup(MF_VERSION, MFSTARTUP_LITE))) {
            fail("Media Foundation indisponible sur ce Windows");
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
        IMFAttributes*   attrs = nullptr;
        IMFSourceReader* reader = nullptr;
        IMFMediaType*    type = nullptr;
        const DWORD      videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        HRESULT hr = mf.createAttributes(&attrs, 1);
        if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = mf.createSourceReaderFromURL(path_.c_str(), attrs, &reader);
        if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(videoStream, TRUE);
        if (SUCCEEDED(hr)) hr = mf.createMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(videoStream, nullptr, type);
        UINT32 width = 0, height = 0;
        LONG   stride = 0;
        const auto readFormat = [&]() -> HRESULT {
            IMFMediaType* actual = nullptr;
            HRESULT r = reader->GetCurrentMediaType(videoStream, &actual);
            if (SUCCEEDED(r)) r = MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &width, &height);
            if (SUCCEEDED(r)) {
                UINT32 s = 0;
                stride = SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) ? static_cast<LONG>(s) : static_cast<LONG>(width * 4);
            }
            release(actual);
            return r;
        };
        if (SUCCEEDED(hr)) hr = readFormat();
        if (SUCCEEDED(hr)) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var))
                && var.vt == VT_UI8) {
                const std::lock_guard<std::mutex> lock(m_);
                duration_ = static_cast<double>(var.uhVal.QuadPart) / 1e7;
            }
            PropVariantClear(&var);
        }
        if (FAILED(hr) || width == 0 || height == 0) {
            char code[16];
            std::snprintf(code, sizeof code, "%08lX", static_cast<unsigned long>(hr));
            fail(std::string("Windows ne d\xC3\xA9" "code pas cette vid\xC3\xA9o (Media Foundation, code ") + code + ")");
        } else {
            bool ended = false;
            for (;;) {
                double seek = -1;
                {
                    std::unique_lock<std::mutex> lock(m_);
                    cv_.wait(lock, [&] { return stop_ || queue_.wantsWork(ended); });
                    if (stop_) break;
                    seek = queue_.takeSeek();
                }
                if (seek >= 0) {
                    PROPVARIANT pos;
                    PropVariantInit(&pos);
                    pos.vt = VT_I8;
                    pos.hVal.QuadPart = static_cast<LONGLONG>(seek * 1e7);
                    (void)reader->SetCurrentPosition(GUID_NULL, pos);
                    PropVariantClear(&pos);
                    ended = false;
                }
                DWORD     stream = 0, flags = 0;
                LONGLONG  stamp = 0;
                IMFSample* sample = nullptr;
                hr = reader->ReadSample(videoStream, 0, &stream, &flags, &stamp, &sample);
                if (FAILED(hr)) {
                    fail("lecture de la vid\xC3\xA9o interrompue (Media Foundation)");
                    break;
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) ended = true;
                if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && FAILED(readFormat())) {
                    release(sample);
                    fail("la vid\xC3\xA9o change de format en cours de route");
                    break;
                }
                if (sample) {
                    IMFMediaBuffer* buffer = nullptr;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                        BYTE* bytes = nullptr;
                        DWORD maxLen = 0, len = 0;
                        if (SUCCEEDED(buffer->Lock(&bytes, &maxLen, &len))) {
                            const std::size_t rowBytes = static_cast<std::size_t>(stride < 0 ? -stride : stride);
                            if (rowBytes >= static_cast<std::size_t>(width) * 4 && len >= rowBytes * height) {
                                Rgba image;
                                image.width = static_cast<int>(width);
                                image.height = static_cast<int>(height);
                                image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
                                for (UINT32 y = 0; y < height; ++y) {
                                    // Un pas negatif : les lignes sont rangees de bas en haut.
                                    const BYTE* row = bytes + rowBytes * (stride < 0 ? height - 1 - y : y);
                                    std::uint8_t* dst = image.pixels.data() + static_cast<std::size_t>(y) * width * 4;
                                    for (UINT32 x = 0; x < width; ++x) {   // BGRX -> RGBA
                                        dst[x * 4 + 0] = row[x * 4 + 2];
                                        dst[x * 4 + 1] = row[x * 4 + 1];
                                        dst[x * 4 + 2] = row[x * 4 + 0];
                                        dst[x * 4 + 3] = 255;
                                    }
                                }
                                const std::lock_guard<std::mutex> lock(m_);
                                queue_.push(static_cast<double>(stamp) / 1e7, std::move(image));   // jetee si un saut est demande
                            }
                            buffer->Unlock();
                        }
                        release(buffer);
                    }
                    release(sample);
                }
            }
        }
        release(type);
        release(reader);
        release(attrs);
        mf.shutdown();
        if (SUCCEEDED(co)) CoUninitialize();
    }

    BlobPtr                 data_;      // garde la ressource tant que le fichier temporaire sert
    std::wstring            path_;
    std::thread             thread_;
    mutable std::mutex      m_;
    std::condition_variable cv_;
    FrameQueue              queue_;     // sous le verrou
    double                  duration_{0};
    bool                    stop_{false};
    bool                    failed_{false};
    std::string             error_;
};

} // namespace

bool mediaFoundationAvailable() { return mfApi().missing.empty(); }

std::unique_ptr<FrameSource> openMediaFoundation(const BlobPtr& data, std::string_view format, std::string* why) {
    if (!data || data->empty()) {
        if (why) *why = "fichier vide";
        return nullptr;
    }
    if (!mediaFoundationAvailable()) {
        if (why) *why = std::string(format) + " : " + mediaFoundationMissing();
        return nullptr;
    }
    std::wstring path = writeTemp(*data, format);
    if (path.empty()) {
        if (why) *why = "fichier temporaire impossible a \xC3\xA9" "crire (dossier TEMP)";
        return nullptr;
    }
    return std::make_unique<MfSource>(data, std::move(path));
}

std::string mediaFoundationMissing() {
    const MfApi& mf = mfApi();
    if (mf.missing.empty()) return {};
    return "Media Foundation manque \xC3\xA0 ce Windows (" + mf.missing + " ; une \xC3\xA9" "dition N : installer le Media Feature Pack) "
           "; sinon, convertir en Motion-JPEG AVI";
}

#else

bool mediaFoundationAvailable() { return false; }
std::string mediaFoundationMissing() { return {}; }

std::unique_ptr<FrameSource> openMediaFoundation(const BlobPtr&, std::string_view, std::string* why) {
    if (why) why->clear();   // le conseil (decodeAdvice) le dit
    return nullptr;
}

#endif

} // namespace hmi::video
