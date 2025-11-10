#include <stdexcept>
#include <system_error>

#include "Globals.hpp"
#include "Logging.hpp"
#include "XenTimeProvider.hpp"

HRESULT CALLBACK
TimeProvOpen(_In_ PWSTR wszName, _In_ TimeProvSysCallbacks *pSysCallbacks, _Out_ TimeProvHandle *phTimeProv) {
    if (CompareStringOrdinal(XenTimeProviderName, -1, wszName, -1, TRUE) == CSTR_EQUAL) {
        try {
            *phTimeProv = new XenTimeProvider(pSysCallbacks);
            return S_OK;
        }
        CATCH_RETURN();
    } else {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
}

HRESULT CALLBACK TimeProvCommand(_In_ TimeProvHandle hTimeProv, _In_ TimeProvCmd eCmd, _In_ TimeProvArgs pvArgs) {
    auto provider = static_cast<XenTimeProvider *>(hTimeProv);

    try {
        switch (eCmd) {
        case TPC_TimeJumped:
            return provider->TimeJumped(static_cast<TpcTimeJumpedArgs *>(pvArgs));
        case TPC_GetSamples:
            return provider->GetSamples(static_cast<TpcGetSamplesArgs *>(pvArgs));
        case TPC_PollIntervalChanged:
            return provider->PollIntervalChanged();
        case TPC_UpdateConfig:
            return provider->UpdateConfig();
        case TPC_Shutdown:
            return provider->Shutdown();
        default:
            return S_OK;
        }
    }
    CATCH_RETURN();
}

HRESULT CALLBACK TimeProvClose(_In_ TimeProvHandle hTimeProv) {
    auto provider = static_cast<XenTimeProvider *>(hTimeProv);

    try {
        delete provider;
        return S_OK;
    }
    CATCH_RETURN();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(ulReasonForCall);
    UNREFERENCED_PARAMETER(lpReserved);
    return TRUE;
}
