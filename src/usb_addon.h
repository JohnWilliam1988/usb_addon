#pragma once
#include <napi.h>
#include <windows.h>
#include <setupapi.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>


class UsbDevice : public Napi::ObjectWrap<UsbDevice> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    UsbDevice(const Napi::CallbackInfo& info);
    ~UsbDevice();

private:
    static Napi::FunctionReference constructor;

    // Windows handles
    HANDLE deviceHandle;
    bool isConnected;
    std::thread notificationThread;
    bool shouldStopNotification;
    HDEVNOTIFY deviceNotificationHandle;
    
    // 数据传输相关
    std::queue<std::vector<uint8_t>> sendQueue;
    std::mutex sendQueueMutex;
    double sendProgress;
    bool isOperationInProgress;
    OVERLAPPED osWrite;
    OVERLAPPED osRead;

    // JavaScript回调函数
    Napi::ThreadSafeFunction tsfn;

    // Node.js方法
    Napi::Value Connect(const Napi::CallbackInfo& info);
    Napi::Value Disconnect(const Napi::CallbackInfo& info);
    Napi::Value SendData(const Napi::CallbackInfo& info);
    Napi::Value SendDataWithResponse(const Napi::CallbackInfo& info);
    Napi::Value GetSendProgress(const Napi::CallbackInfo& info);
    Napi::Value StartHotplugMonitor(const Napi::CallbackInfo& info);
    Napi::Value StopHotplugMonitor(const Napi::CallbackInfo& info);

    // 内部方法
    void NotificationThreadProc();
    void ProcessSendQueue();
    bool InitializeDevice(HANDLE deviceHandle);
    std::string GetDevicePath(WORD vendorId, WORD productId);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
