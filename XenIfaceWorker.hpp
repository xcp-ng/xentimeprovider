#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>

#include <wil/resource.h>

class XenIfaceWorker {
public:
    XenIfaceWorker();
    XenIfaceWorker(const XenIfaceWorker &) = delete;
    XenIfaceWorker &operator=(const XenIfaceWorker &) = delete;

    std::tuple<std::unique_lock<std::mutex>, HANDLE> GetDevice();

    ~XenIfaceWorker();

private:
    void WorkerFunc(std::stop_token stop);
    _Requires_lock_held_(_mutex) HRESULT RefreshDevices();

    _Pre_satisfies_(EventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) static DWORD CALLBACK OnCmNotification(
        _In_ HCMNOTIFICATION hNotify,
        _In_opt_ PVOID Context,
        _In_ CM_NOTIFY_ACTION Action,
        _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
        _In_ DWORD EventDataSize);

    _Pre_satisfies_(EventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) DWORD OnCmNotification(
        _In_ HCMNOTIFICATION hNotify,
        _In_ CM_NOTIFY_ACTION Action,
        _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
        _In_ DWORD EventDataSize);

    struct worker_state {
        std::mutex Mutex;
        std::condition_variable Signal;
        std::list<CM_NOTIFY_ACTION> Requests;
        wil::unique_hcmnotification DeviceListener;
        wil::unique_hfile Device;
    } _state;
    std::jthread _worker;
};
