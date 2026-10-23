// =============================================================================
//  app/GlisserDehors.cpp - 1.12.27 : GLISSER DU TEXTE HORS DE LA FENETRE
//  (voir GlisserDehors.hpp)
// -----------------------------------------------------------------------------
//  Sous Windows : le glisser-deposer d'OLE (DoDragDrop), avec le texte en
//  CF_UNICODETEXT - ce que la fenetre d'en face (une autre fenetre d'IHM
//  Studio : SDL le recoit en SDL_EVENT_DROP_TEXT ; Excel, le Bloc-notes)
//  accepte. Ailleurs : rien (-1), le glisser reste dans la fenetre.
// =============================================================================
#include "GlisserDehors.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <ole2.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ole32.lib")
#  endif
#  include <cstring>
#  include <string>
#  if defined(__GNUC__)
// COM : un objet se detruit par son Release(), jamais par un delete sur l'interface.
#    pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#  endif

namespace app::glisser {
namespace {

// Les identifiants des interfaces, ici : pas besoin de libuuid.
const IID kIUnknown        = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const IID kIDataObject     = {0x0000010e, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const IID kIDropSource     = {0x00000121, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const IID kIEnumFORMATETC  = {0x00000103, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

bool same(REFIID a, const IID& b) { return std::memcmp(&a, &b, sizeof(IID)) == 0; }

FORMATETC texte() {
    FORMATETC f{};
    f.cfFormat = CF_UNICODETEXT;
    f.ptd = nullptr;
    f.dwAspect = DVASPECT_CONTENT;
    f.lindex = -1;
    f.tymed = TYMED_HGLOBAL;
    return f;
}

class Formats final : public IEnumFORMATETC {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (same(riid, kIUnknown) || same(riid, kIEnumFORMATETC)) {
            *out = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out, ULONG* fetched) override {
        ULONG n = 0;
        if (count > 0 && out && at_ == 0) {
            out[0] = texte();
            n = 1;
            at_ = 1;
        }
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        at_ += count;
        return at_ <= 1 ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        at_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        auto* c = new Formats();
        c->at_ = at_;
        *out = c;
        return S_OK;
    }
private:
    LONG  refs_{1};
    ULONG at_{0};
};

class Donnees final : public IDataObject {
public:
    explicit Donnees(std::wstring text) : text_(std::move(text)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (same(riid, kIUnknown) || same(riid, kIDataObject)) {
            *out = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* m) override {
        if (!f || !m) return E_INVALIDARG;
        if (f->cfFormat != CF_UNICODETEXT || !(f->tymed & TYMED_HGLOBAL) || f->dwAspect != DVASPECT_CONTENT) return DV_E_FORMATETC;
        const SIZE_T bytes = (text_.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!h) return E_OUTOFMEMORY;
        void* p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return E_OUTOFMEMORY;
        }
        std::memcpy(p, text_.c_str(), bytes);
        GlobalUnlock(h);
        m->tymed = TYMED_HGLOBAL;
        m->hGlobal = h;
        m->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        if (!f) return E_INVALIDARG;
        return f->cfFormat == CF_UNICODETEXT && (f->tymed & TYMED_HGLOBAL) && f->dwAspect == DVASPECT_CONTENT ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        if (direction != DATADIR_GET) {
            *out = nullptr;
            return E_NOTIMPL;
        }
        *out = new Formats();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    LONG         refs_{1};
    std::wstring text_;
};

class Source final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (same(riid, kIUnknown) || same(riid, kIDropSource)) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    // Echap annule ; le bouton relache depose.
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    LONG refs_{1};
};

std::wstring large(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

} // namespace

int texteDehors(std::string_view utf8) {
    if (utf8.empty()) return -1;
    const HRESULT init = OleInitialize(nullptr);   // S_FALSE : deja fait (SDL l'a fait pour ses depots)
    if (FAILED(init)) return -1;
    auto* data = new Donnees(large(utf8));
    auto* source = new Source();
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT r = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    data->Release();
    source->Release();
    OleUninitialize();
    if (r == DRAGDROP_S_DROP && (effect & DROPEFFECT_COPY)) return 1;
    return 0;
}

} // namespace app::glisser

#else

namespace app::glisser {
int texteDehors(std::string_view) { return -1; }
} // namespace app::glisser

#endif
