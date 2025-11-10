#include <vector>

#include <wil/result.h>
#include <wil/filesystem.h>

#include "Logging.hpp"
#include "XenIfaceWorker.hpp"
#include "xeniface_ioctls.h"

#define RETURN_IF_CR_FAILED(cr) \
    do { \
        CONFIGRET _cr = (cr); \
        if (_cr != CR_SUCCESS) { \
            return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(_cr, ERROR_GEN_FAILURE)); \
        } \
    } while (0)

#define THROW_IF_CR_FAILED(cr) \
    do { \
        CONFIGRET _cr = (cr); \
        if (_cr != CR_SUCCESS) { \
            THROW_HR(CM_MapCrToWin32Err(_cr, ERROR_GEN_FAILURE)); \
        } \
    } while (0)

static std::vector<std::wstring> ParseMultiStrings(_In_reads_(count) const WCHAR *buf, size_t count) {
    std::vector<std::wstring> strings;
    size_t first = 0;
    for (size_t i = 0; i < count; i++) {
        if (buf[i] == 0) {
            strings.emplace_back(buf + first, i - first);
            first = i + 1;
        }
    }
    if (strings.back().empty()) {
        strings.pop_back();
    }
    return strings;
}

static HRESULT GetDeviceInterfaceList(
    _Out_ std::vector<WCHAR> &list,
    _In_ LPCGUID interfaceClassGuid,
    _In_opt_ DEVINSTID_W deviceID,
    _In_ ULONG flags) {
    ULONG devListLen = 0;
    CONFIGRET cr;

    list.clear();

    do {
        cr = CM_Get_Device_Interface_List_Size(&devListLen, const_cast<LPGUID>(interfaceClassGuid), nullptr, flags);
        if (cr != CR_SUCCESS)
            DebugLog("CM_Get_Device_Interface_List_Size failed %x", cr);
        RETURN_IF_CR_FAILED(cr);

        list.resize(devListLen);

        cr = CM_Get_Device_Interface_List(
            const_cast<LPGUID>(interfaceClassGuid),
            deviceID,
            list.data(),
            static_cast<ULONG>(list.size()),
            flags);
    } while (cr == CR_BUFFER_SMALL);
    if (cr != CR_SUCCESS)
        DebugLog("CM_Get_Device_Interface_List failed %x", cr);
    RETURN_IF_CR_FAILED(cr);

    return S_OK;
}

_Use_decl_annotations_ DWORD XenIfaceDevice::DeviceHandleCallback(
    HCMNOTIFICATION notifyHandle,
    PVOID context,
    CM_NOTIFY_ACTION action,
    PCM_NOTIFY_EVENT_DATA eventData,
    DWORD eventDataSize) {
    auto self = static_cast<XenIfaceDevice *>(context)->shared_from_this();

    UNREFERENCED_PARAMETER(notifyHandle);
    UNREFERENCED_PARAMETER(eventData);
    UNREFERENCED_PARAMETER(eventDataSize);

    switch (action) {
    case CM_NOTIFY_ACTION_DEVICEQUERYREMOVE:
    case CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED:
        // Must close immediately to avoid failing DEVICEQUERYREMOVE
        OutputDebugStringA("CM_NOTIFY_ACTION_DEVICEQUERYREMOVE/FAILED");
        self->GetHandle().reset();
        break;
    }

    self->_worker->QueueRequest(self->_worker->Lock(), self, action);

    return ERROR_SUCCESS;
}

XenIfaceDevice::XenIfaceDevice(wil::unique_hfile &&handle, const std::wstring &path, _In_ XenIfaceWorker *worker)
    : _handle(handle), _path(path), _worker(worker) {
    CM_NOTIFY_FILTER filter{
        .cbSize = sizeof(CM_NOTIFY_FILTER),
        .Flags = 0,
        .FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE,
        .Reserved = 0,
        .u = {.DeviceHandle = {.hTarget = handle.get()}},
    };
    wil::unique_hcmnotification newListener;
    auto cr = CM_Register_Notification(&filter, this, &DeviceHandleCallback, &newListener);
    if (cr != CR_SUCCESS)
        DebugLog("CM_Register_Notification failed %x", cr);
    THROW_IF_CR_FAILED(cr);
}

HRESULT XenIfaceDevice::make(
    _Out_ std::shared_ptr<XenIfaceDevice> &object,
    _In_ wil::unique_hfile &&handle,
    _In_ const std::wstring &path,
    _In_ XenIfaceWorker *worker) {
    try {
        object = std::make_shared<XenIfaceDevice>(handle, path, worker);
    }
    CATCH_RETURN();
}

XenIfaceWorker::XenIfaceWorker() : _worker([this](std::stop_token stop) { WorkerFunc(stop); }) {}

XenIfaceWorker::~XenIfaceWorker() {
    _worker.request_stop();
    std::lock_guard lock(_mutex);
    _signal.notify_one();
}

std::shared_ptr<XenIfaceDevice> XenIfaceWorker::GetDevice(const std::unique_lock<std::mutex> &lock) {
    return _active;
}

void XenIfaceWorker::QueueRequest(
    std::unique_lock<std::mutex> &&lock,
    std::shared_ptr<XenIfaceDevice> target,
    CM_NOTIFY_ACTION action) {
    {
        auto _lock = std::move(lock);
        _requests.emplace_back(XenIfaceWorkerRequest{.Target = std::move(target), .Action = action});
    }
    _signal.notify_one();
}

_Use_decl_annotations_ DWORD CALLBACK XenIfaceWorker::CmListenerCallback(
    HCMNOTIFICATION notifyHandle,
    PVOID context,
    CM_NOTIFY_ACTION action,
    PCM_NOTIFY_EVENT_DATA eventData,
    DWORD eventDataSize) {
    auto self = static_cast<XenIfaceWorker *>(context);

    UNREFERENCED_PARAMETER(notifyHandle);
    UNREFERENCED_PARAMETER(eventData);
    UNREFERENCED_PARAMETER(eventDataSize);

    {
        std::lock_guard lock(self->_mutex);
        self->_requests.emplace_back(action);
    }
    self->_signal.notify_one();

    return ERROR_SUCCESS;
}

HRESULT XenIfaceWorker::RefreshDevices(std::list<std::shared_ptr<XenIfaceDevice>> &tombstones) {
    OutputDebugStringA("XenIfaceWorker::RefreshDevices");

    std::vector<WCHAR> buffer;
    auto hr = GetDeviceInterfaceList(buffer, &GUID_INTERFACE_XENIFACE, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (FAILED(hr))
        DebugLog("GetDeviceInterfaceList failed %x", hr);
    RETURN_IF_FAILED(hr);

    auto interfaces = ParseMultiStrings(buffer.data(), buffer.size());
    if (interfaces.size() == 0) {
        OutputDebugStringA("Interface list empty");
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    OutputDebugStringA("Interface list:");
    for (const auto &iface : interfaces)
        OutputDebugString(iface.c_str());

    if (_active && _active->GetHandle().is_valid()) {
        OutputDebugStringA("Device valid, skipping refresh");
        return S_FALSE;
    } else if (_active) {
        tombstones.emplace_back(std::move(_active));
    }

    auto [newDevice, err] = wil::try_open_file(interfaces[0].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (!newDevice.is_valid())
        DebugLog("open(%S) failed %x", interfaces[0].c_str(), err);
    RETURN_HR_IF(HRESULT_FROM_WIN32(err), !newDevice.is_valid());

    RETURN_IF_FAILED(XenIfaceDevice::make(_active, std::move(newDevice), interfaces[0], this));

    return S_OK;
}

void XenIfaceWorker::WorkerFunc(std::stop_token stop) {
    HRESULT hr;
    std::list<std::shared_ptr<XenIfaceDevice>> tombstones;

    CM_NOTIFY_FILTER filter{
        .cbSize = sizeof(CM_NOTIFY_FILTER),
        .Flags = 0,
        .FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE,
        .Reserved = 0,
        .u = {.DeviceInterface = {.ClassGuid = GUID_INTERFACE_XENIFACE}},
    };

    wil::unique_hcmnotification cmListener;
    auto cr = CM_Register_Notification(&filter, this, &CmListenerCallback, &cmListener);
    if (cr != CR_SUCCESS)
        return;

    {
        std::lock_guard lock(_mutex);
        hr = RefreshDevices(tombstones);
        if (FAILED(hr))
            DebugLog("RefreshDevices failed %x", hr);
    }

    while (1) {
        {
            std::unique_lock lock(_mutex);
            _signal.wait(lock);
            if (stop.stop_requested())
                break;

            while (!_requests.empty()) {
                auto request = _requests.front();
                _requests.pop_front();
                switch (request.Action) {
                case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
                    OutputDebugStringA("CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL");
                    hr = RefreshDevices(tombstones);
                    if (FAILED(hr))
                        DebugLog("RefreshDevices failed %x", hr);
                    break;

                case CM_NOTIFY_ACTION_DEVICEREMOVEPENDING:
                case CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE:
                    OutputDebugStringA("CM_NOTIFY_ACTION_DEVICEREMOVEPENDING");
                    if (request.Target == _active)
                        _active.reset();
                    tombstones.emplace_back(std::move(request.Target));
                    break;
                }
            }
        }

        // Closing old listeners must be done outside of the lock, since CM_Unregister_Notification will wait for
        // callbacks to finish
        tombstones.clear();
    }
}
