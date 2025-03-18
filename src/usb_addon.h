#pragma once
#include <napi.h>
#include <windows.h>
#include <setupapi.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>

// GUID for WinUSB devices
extern "C" const GUID GUID_DEVINTERFACE_USB_DEVICE = 
{ 0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED} };

//{0x28d78fad, 0x5a12, 0x11d1, 0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2};

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

    // JavaScript回调函数
    Napi::ThreadSafeFunction tsfn;

    // Node.js方法
    Napi::Value Connect(const Napi::CallbackInfo& info);
    Napi::Value Disconnect(const Napi::CallbackInfo& info);
    Napi::Value SendData(const Napi::CallbackInfo& info);
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
