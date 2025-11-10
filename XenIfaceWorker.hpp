#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>

#include <wil/resource.h>

class XenIfaceDevice : public std::enable_shared_from_this<XenIfaceDevice> {
public:
    static HRESULT make(
        _Out_ std::shared_ptr<XenIfaceDevice> &device,
        _In_ wil::unique_hfile &&handle,
        _In_ const std::wstring &path,
        _In_ XenIfaceWorker *worker);

    XenIfaceDevice(const XenIfaceDevice &) = delete;
    XenIfaceDevice &operator=(const XenIfaceDevice &) = delete;
    XenIfaceDevice(XenIfaceDevice &&) = default;
    XenIfaceDevice &operator=(XenIfaceDevice &&) = default;

    wil::unique_hfile &GetHandle() {
        return _handle;
    }
    const std::wstring &GetPath() const {
        return _path;
    }

    _Pre_satisfies_(eventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) static DWORD CALLBACK DeviceHandleCallback(
        _In_ HCMNOTIFICATION notifyHandle,
        _In_opt_ PVOID context,
        _In_ CM_NOTIFY_ACTION action,
        _In_reads_bytes_(eventDataSize) PCM_NOTIFY_EVENT_DATA eventData,
        _In_ DWORD eventDataSize);

private:
    XenIfaceDevice(wil::unique_hfile &&handle, const std::wstring &path, XenIfaceWorker *worker);

    wil::unique_hcmnotification _listener;
    wil::unique_hfile _handle;
    std::wstring _path;
    XenIfaceWorker *_worker;
};

struct XenIfaceWorkerRequest {
    std::shared_ptr<XenIfaceDevice> Target;
    CM_NOTIFY_ACTION Action;
};

class XenIfaceWorker {
public:
    XenIfaceWorker();
    XenIfaceWorker(const XenIfaceWorker &) = delete;
    XenIfaceWorker &operator=(const XenIfaceWorker &) = delete;

    std::unique_lock<std::mutex> Lock() {
        return std::unique_lock(_mutex);
    }
    std::shared_ptr<XenIfaceDevice> GetDevice(const std::unique_lock<std::mutex> &lock);
    void
    QueueRequest(std::unique_lock<std::mutex> &&lock, std::shared_ptr<XenIfaceDevice> target, CM_NOTIFY_ACTION action);

    ~XenIfaceWorker();

private:
    void WorkerFunc(std::stop_token stop);
    HRESULT RefreshDevices(std::list<std::shared_ptr<XenIfaceDevice>> &tombstones);

    _Pre_satisfies_(eventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) static DWORD CALLBACK CmListenerCallback(
        _In_ HCMNOTIFICATION notifyHandle,
        _In_opt_ PVOID context,
        _In_ CM_NOTIFY_ACTION action,
        _In_reads_bytes_(eventDataSize) PCM_NOTIFY_EVENT_DATA eventData,
        _In_ DWORD eventDataSize);

    struct {
        std::mutex _mutex;
        std::condition_variable _signal;
        _Guarded_by_(_mutex) std::list<XenIfaceWorkerRequest> _requests;
        _Guarded_by_(_mutex) std::shared_ptr<XenIfaceDevice> _active;
    };
    std::jthread _worker;
};
