#pragma once
#include <napi.h>
#include <windows.h>
#include <setupapi.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <iostream>

// 事件类型
enum class EventType {
    HOTPLUG,    // 热插拔事件
    CMD_RESPONSE, // 命令响应
    ERR       // 错误事件
};

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
    double sendProgress;
    bool isOperationInProgress;
    
    // JavaScript回调函数
    Napi::ThreadSafeFunction tsfn;  // 用于所有事件回调

    // Node.js方法
    Napi::Value Connect(const Napi::CallbackInfo& info);
    Napi::Value Disconnect(const Napi::CallbackInfo& info);
    Napi::Value SendPlt(const Napi::CallbackInfo& info);
    Napi::Value SendCmd(const Napi::CallbackInfo& info);
    Napi::Value GetSendProgress(const Napi::CallbackInfo& info);
    Napi::Value StartHotplugMonitor(const Napi::CallbackInfo& info);
    Napi::Value StopHotplugMonitor(const Napi::CallbackInfo& info);

    // 内部方法
    void NotificationThreadProc();
    void ProcessSendQueue();
    bool InitializeDevice(HANDLE deviceHandle);
    std::string GetDevicePath(WORD vendorId, WORD productId);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    WORD currentVendorId;    // 添加当前设备的 VID
    WORD currentProductId;   // 添加当前设备的 PID
};
