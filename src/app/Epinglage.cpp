// =============================================================================
//  app/Epinglage.cpp - 1.8.0 : voir Epinglage.hpp
// -----------------------------------------------------------------------------
//  Windows.UI.Shell.TaskbarManager par son interface binaire (ABI), sans
//  C++/WinRT ni bibliotheque a lier : le meme code pour MSVC et MinGW. Les
//  fonctions de WinRT sont prises dans combase.dll a l'execution ; les
//  interfaces (leurs IID et l'ordre de leurs methodes) sont celles de
//  windows.ui.shell.h du SDK Windows 10 (16299 et suivants).
//  Ailleurs que sous Windows : Indisponible.
// =============================================================================
#include "Epinglage.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <inspectable.h>
#include <winstring.h>

#include <cwchar>
#endif

namespace app::epinglage {

#ifdef _WIN32
// Des interfaces COM : jamais de destructeur virtuel (la duree de vie passe par Release).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
namespace {

// IAsyncOperation<bool> : on ne fait que la garder vivante.
struct IOperationBooleen : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* gestionnaire) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** gestionnaire) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(unsigned char* resultat) = 0;
protected:
    ~IOperationBooleen() = default;
};

// Windows.UI.Shell.ITaskbarManager : {87490A19-1AD9-49F4-B2E8-86738DC5AC40}
struct ITaskbarManagerAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsSupported(unsigned char* valeur) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPinningAllowed(unsigned char* valeur) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsCurrentAppPinnedAsync(IOperationBooleen** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsAppListEntryPinnedAsync(IInspectable* entree, IOperationBooleen** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestPinCurrentAppAsync(IOperationBooleen** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestPinAppListEntryAsync(IInspectable* entree, IOperationBooleen** operation) = 0;
protected:
    ~ITaskbarManagerAbi() = default;
};

// Windows.UI.Shell.ITaskbarManagerStatics : {DB32AB74-DE52-4FE6-B7B6-95FF9F8395DF}
struct ITaskbarManagerStaticsAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetDefault(ITaskbarManagerAbi** resultat) = 0;
protected:
    ~ITaskbarManagerStaticsAbi() = default;
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

const IID kIidStatics = {0xdb32ab74, 0xde52, 0x4fe6, {0xb7, 0xb6, 0x95, 0xff, 0x9f, 0x83, 0x95, 0xdf}};

using FnRoInitialize = HRESULT(WINAPI*)(int);
using FnRoGetActivationFactory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using FnWindowsCreateString = HRESULT(WINAPI*)(const wchar_t*, UINT32, HSTRING*);
using FnWindowsDeleteString = HRESULT(WINAPI*)(HSTRING);

template <class F>
F fonction(HMODULE m, const char* nom) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, nom)));
}

// La demande en cours : gardee jusqu'a la suivante (Windows y repond seul).
IOperationBooleen*& operationEnCours() {
    static IOperationBooleen* op = nullptr;
    return op;
}

} // namespace

Resultat demanderBarreDesTaches() {
    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (!combase) return Resultat::Indisponible;
    const auto roInitialize = fonction<FnRoInitialize>(combase, "RoInitialize");
    const auto roGetActivationFactory = fonction<FnRoGetActivationFactory>(combase, "RoGetActivationFactory");
    const auto creerChaine = fonction<FnWindowsCreateString>(combase, "WindowsCreateString");
    const auto effacerChaine = fonction<FnWindowsDeleteString>(combase, "WindowsDeleteString");
    if (!roInitialize || !roGetActivationFactory || !creerChaine || !effacerChaine) return Resultat::Indisponible;

    // Le fil de la fenetre : a un seul fil (S_FALSE, ou RPC_E_CHANGED_MODE s'il est deja
    // initialise autrement : les deux conviennent).
    (void)roInitialize(0 /* RO_INIT_SINGLETHREADED */);

    const wchar_t* classe = L"Windows.UI.Shell.TaskbarManager";
    HSTRING nom = nullptr;
    if (FAILED(creerChaine(classe, static_cast<UINT32>(std::wcslen(classe)), &nom))) return Resultat::Indisponible;
    ITaskbarManagerStaticsAbi* statiques = nullptr;
    const HRESULT hr = roGetActivationFactory(nom, kIidStatics, reinterpret_cast<void**>(&statiques));
    effacerChaine(nom);
    if (FAILED(hr) || !statiques) return Resultat::Indisponible;

    ITaskbarManagerAbi* barre = nullptr;
    const HRESULT hr2 = statiques->GetDefault(&barre);
    statiques->Release();
    if (FAILED(hr2) || !barre) return Resultat::Indisponible;

    Resultat r = Resultat::NonPermis;
    unsigned char oui = 0;
    if (SUCCEEDED(barre->get_IsSupported(&oui)) && oui) {
        oui = 0;
        if (SUCCEEDED(barre->get_IsPinningAllowed(&oui)) && oui) {
            IOperationBooleen* op = nullptr;
            if (SUCCEEDED(barre->RequestPinCurrentAppAsync(&op)) && op) {
                if (operationEnCours()) operationEnCours()->Release();
                operationEnCours() = op;
                r = Resultat::Demande;
            }
        }
    }
    barre->Release();
    return r;
}
#else
Resultat demanderBarreDesTaches() { return Resultat::Indisponible; }
#endif

} // namespace app::epinglage
