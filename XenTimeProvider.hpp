#pragma once

#include <optional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TimeProv.h>

#include "XenIfaceWorker.hpp"

static constexpr auto XenTimeProviderName = L"XenTimeProvider";

class XenTimeProvider {
public:
    XenTimeProvider(_In_ TimeProvSysCallbacks *callbacks);
    XenTimeProvider(const XenTimeProvider &) = delete;
    XenTimeProvider &operator=(const XenTimeProvider &) = delete;
    XenTimeProvider(XenTimeProvider &&) = default;
    XenTimeProvider &operator=(XenTimeProvider &&) = default;

    HRESULT TimeJumped(_In_ TpcTimeJumpedArgs *args);
    HRESULT GetSamples(_Out_ TpcGetSamplesArgs *args);
    HRESULT PollIntervalChanged();
    HRESULT UpdateConfig();
    HRESULT Shutdown();

private:
    HRESULT Update();

    TimeProvSysCallbacks _callbacks;
    XenIfaceWorker _worker;
    std::optional<TimeSample> _sample;
};
