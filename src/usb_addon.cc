#include "usb_addon.h"
#include <dbt.h>
#include <iostream>
#include <setupapi.h>
#include <initguid.h>
#include <usbprint.h>

Napi::FunctionReference UsbDevice::constructor;

Napi::Object UsbDevice::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "UsbDevice", {
        InstanceMethod("connect", &UsbDevice::Connect),
        InstanceMethod("disconnect", &UsbDevice::Disconnect),
        InstanceMethod("sendData", &UsbDevice::SendData),
        InstanceMethod("getSendProgress", &UsbDevice::GetSendProgress),
        InstanceMethod("startHotplugMonitor", &UsbDevice::StartHotplugMonitor),
        InstanceMethod("stopHotplugMonitor", &UsbDevice::StopHotplugMonitor)
    });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("UsbDevice", func);
    return exports;
}

UsbDevice::UsbDevice(const Napi::CallbackInfo& info) 
    : Napi::ObjectWrap<UsbDevice>(info) {
    deviceHandle = INVALID_HANDLE_VALUE;
    isConnected = false;
    shouldStopNotification = false;
    deviceNotificationHandle = NULL;
    sendProgress = 0.0;
}

UsbDevice::~UsbDevice() {
    // 安全清理资源
    if (isConnected) {
        if (deviceHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
        isConnected = false;
    }

    if (notificationThread.joinable()) {
        shouldStopNotification = true;
        notificationThread.join();
    }
}

bool UsbDevice::InitializeDevice(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Invalid device handle" << std::endl;
        return false;
    }
    
    // 对于USB打印机，我们不需要配置串口参数
    // 只需检查设备句柄是否有效
    return true;
}

std::string UsbDevice::GetDevicePath(WORD vendorId, WORD productId) {
    // 使用打印机类 GUID
    HDEVINFO deviceInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USBPRINT, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "SetupDiGetClassDevsA failed: " << GetLastError() << std::endl;
        return "";
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_USBPRINT, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiGetDeviceInterfaceDetailA(deviceInfo, &interfaceData, detailData, requiredSize, NULL, &devInfoData)) {
            std::cerr << "SetupDiGetDeviceInterfaceDetailA failed: " << GetLastError() << std::endl;
            free(detailData);
            continue;
        }

        std::string devicePath = detailData->DevicePath;
        free(detailData);

        // 获取设备硬件ID，包含VID/PID信息
        char hardwareId[256] = {0};
        if (!SetupDiGetDeviceRegistryPropertyA(deviceInfo, &devInfoData, SPDRP_HARDWAREID, NULL, 
                                             (PBYTE)hardwareId, sizeof(hardwareId), NULL)) {
            std::cerr << "SetupDiGetDeviceRegistryPropertyA failed: " << GetLastError() << std::endl;
            continue;
        }

        // 打印硬件ID以便调试
        std::cout << "Device " << i << " Hardware ID: " << hardwareId << std::endl;
        std::cout << "Device Path: " << devicePath << std::endl;

        // 如果VID/PID为0，则匹配任何设备
        if (vendorId == 0 && productId == 0) {
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return devicePath;
        }

        // 检查VID/PID
        char vidPattern[10];
        char pidPattern[10];
        sprintf_s(vidPattern, "VID_%04X", vendorId);
        sprintf_s(pidPattern, "PID_%04X", productId);

        // 检查硬件ID是否包含匹配的VID和PID
        if (strstr(hardwareId, vidPattern) && strstr(hardwareId, pidPattern)) {
            std::cout << "Found matching device with VID: 0x" << std::hex << vendorId 
                      << " PID: 0x" << productId << std::dec << std::endl;
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return devicePath;
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return "";
}

Napi::Value UsbDevice::Connect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        throw Napi::TypeError::New(env, "Wrong number of arguments");
    }

    WORD vendorId = (WORD)info[0].As<Napi::Number>().Uint32Value();
    WORD productId = (WORD)info[1].As<Napi::Number>().Uint32Value();

    std::cout << "Trying to connect to device with VID: 0x" << std::hex << vendorId 
              << " PID: 0x" << productId << std::dec << std::endl;

    std::string devicePath = GetDevicePath(vendorId, productId);
    if (devicePath.empty()) {
        return Napi::Boolean::New(env, false);
    }

    // 打开设备进行写入
    deviceHandle = CreateFileA(devicePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (deviceHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cerr << "Failed to open device, error: " << error << std::endl;
        return Napi::Boolean::New(env, false);
    }

    if (!InitializeDevice(deviceHandle)) {
        CloseHandle(deviceHandle);
        deviceHandle = INVALID_HANDLE_VALUE;
        return Napi::Boolean::New(env, false);
    }

    isConnected = true;
    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::Disconnect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (isConnected) {
        if (deviceHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
        isConnected = false;
    }

    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::SendData(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!isConnected) {
        throw Napi::Error::New(env, "Device not connected");
    }

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        throw Napi::TypeError::New(env, "Buffer expected");
    }

    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    
    DWORD bytesWritten = 0;
    
    // 对于打印机，我们使用同步写入
    if (!WriteFile(deviceHandle, buffer.Data(), buffer.Length(), &bytesWritten, NULL)) {
        DWORD error = GetLastError();
        std::cerr << "Failed to write data, error: " << error << std::endl;
        throw Napi::Error::New(env, "Failed to write data to device");
    }

    return Napi::Number::New(env, bytesWritten);
}

Napi::Value UsbDevice::GetSendProgress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    return Napi::Number::New(env, sendProgress);
}

LRESULT CALLBACK UsbDevice::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DEVICECHANGE) {
        auto device = reinterpret_cast<UsbDevice*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (device && device->tsfn) {
            bool isAttached = (wParam == DBT_DEVICEARRIVAL);
            device->tsfn.BlockingCall(&isAttached, [](Napi::Env env, Napi::Function jsCallback, bool* data) {
                jsCallback.Call({Napi::Boolean::New(env, *data)});
            });
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void UsbDevice::NotificationThreadProc() {
    // 鍒涘缓闅愯棌绐楀彛鏉ユ帴鏀惰澶囬€氱煡
    WNDCLASSEXA wndClass = { sizeof(WNDCLASSEXA) };
    wndClass.lpfnWndProc = WindowProc;
    wndClass.lpszClassName = "UsbNotificationClass";
    RegisterClassExA(&wndClass);

    HWND hwnd = CreateWindowExA(0, "UsbNotificationClass", "UsbNotificationWindow",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { 0 };
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

    deviceNotificationHandle = RegisterDeviceNotification(hwnd,
        &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    MSG msg;
    while (!shouldStopNotification && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (deviceNotificationHandle) {
        UnregisterDeviceNotification(deviceNotificationHandle);
        deviceNotificationHandle = nullptr;
    }

    DestroyWindow(hwnd);
    UnregisterClassA("UsbNotificationClass", nullptr);
}

Napi::Value UsbDevice::StartHotplugMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected callback function")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    tsfn = Napi::ThreadSafeFunction::New(
        env,
        info[0].As<Napi::Function>(),
        "HotplugCallback",
        0,
        1
    );

    shouldStopNotification = false;
    notificationThread = std::thread(&UsbDevice::NotificationThreadProc, this);

    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::StopHotplugMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    try {
        // 检查线程是否在运行
        if (!notificationThread.joinable()) {
            return Napi::Boolean::New(env, true);
        }

        // 安全地停止线程
        shouldStopNotification = true;
        
        // 等待线程结束
        if (notificationThread.joinable()) {
            notificationThread.join();
        }

        // 清理通知句柄
        if (deviceNotificationHandle) {
            UnregisterDeviceNotification(deviceNotificationHandle);
            deviceNotificationHandle = nullptr;
        }

        return Napi::Boolean::New(env, true);
    } catch (const std::exception& e) {
        throw Napi::Error::New(env, std::string("Error in StopHotplugMonitor: ") + e.what());
    }
}

void UsbDevice::ProcessSendQueue() {
    // 简单实现
}

// 初始化导出函数
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return UsbDevice::Init(env, exports);
}

NODE_API_MODULE(usb_addon, Init) 
