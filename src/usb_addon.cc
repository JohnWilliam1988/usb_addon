#include "usb_addon.h"
#include <dbt.h>
#include <iostream>
#include <setupapi.h>
#include <initguid.h>
#include <usbprint.h>
#include <regex>

Napi::FunctionReference UsbDevice::constructor;

Napi::Object UsbDevice::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "UsbDevice", {
        InstanceMethod("connect", &UsbDevice::Connect),
        InstanceMethod("disconnect", &UsbDevice::Disconnect),
        InstanceMethod("sendPlt", &UsbDevice::SendPlt),
        InstanceMethod("sendCmd", &UsbDevice::SendCmd),
        InstanceMethod("getSendProgress", &UsbDevice::GetSendProgress),
        InstanceMethod("startHotplugMonitor", &UsbDevice::StartHotplugMonitor),
        InstanceMethod("stopHotplugMonitor", &UsbDevice::StopHotplugMonitor)
    });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("UsbDevice", func);
    return exports;
}

UsbDevice::UsbDevice(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<UsbDevice>(info)
{
    deviceHandle = INVALID_HANDLE_VALUE;
    isConnected = false;
    shouldStopNotification = false;
    deviceNotificationHandle = NULL;
    sendProgress = 0.0;
    isOperationInProgress = false;
}

UsbDevice::~UsbDevice()
{
    // 安全清理资源
    if (isConnected)
    {
        if (deviceHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
        isConnected = false;
    }

    if (notificationThread.joinable())
    {
        shouldStopNotification = true;
        notificationThread.join();
    }
}

bool UsbDevice::InitializeDevice(HANDLE handle)
{
    if (handle == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Invalid device handle" << std::endl;
        return false;
    }

    // 对于USB打印机，我们不需要配置串口参数
    // 只需检查设备句柄是否有效
    return true;
}

std::string UsbDevice::GetDevicePath(WORD vendorId, WORD productId)
{
    // 使用打印机类 GUID
    HDEVINFO deviceInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USBPRINT, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE)
    {
        std::cerr << "SetupDiGetClassDevsA failed: " << GetLastError() << std::endl;
        return "";
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_USBPRINT, i, &interfaceData); i++)
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiGetDeviceInterfaceDetailA(deviceInfo, &interfaceData, detailData, requiredSize, NULL, &devInfoData))
        {
            std::cerr << "SetupDiGetDeviceInterfaceDetailA failed: " << GetLastError() << std::endl;
            free(detailData);
            continue;
        }

        std::string devicePath = detailData->DevicePath;
        free(detailData);

        // 获取设备硬件ID，包含VID/PID信息
        char hardwareId[256] = {0};
        if (!SetupDiGetDeviceRegistryPropertyA(deviceInfo, &devInfoData, SPDRP_HARDWAREID, NULL,
                                               (PBYTE)hardwareId, sizeof(hardwareId), NULL))
        {
            std::cerr << "SetupDiGetDeviceRegistryPropertyA failed: " << GetLastError() << std::endl;
            continue;
        }

        // 打印硬件ID以便调试
        std::cout << "Device " << i << " Hardware ID: " << hardwareId << std::endl;
        std::cout << "Device Path: " << devicePath << std::endl;

        // 如果VID/PID为0，则匹配任何设备
        if (vendorId == 0 && productId == 0)
        {
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return devicePath;
        }

        // 检查VID/PID
        char vidPattern[10];
        char pidPattern[10];
        sprintf_s(vidPattern, "VID_%04X", vendorId);
        sprintf_s(pidPattern, "PID_%04X", productId);

        // 检查硬件ID是否包含匹配的VID和PID
        if (strstr(hardwareId, vidPattern) && strstr(hardwareId, pidPattern))
        {
            std::cout << "Found matching device with VID: 0x" << std::hex << vendorId
                      << " PID: 0x" << productId << std::dec << std::endl;
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return devicePath;
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return "";
}

Napi::Value UsbDevice::Connect(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 2)
    {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 如果已经连接，先断开连接并清理资源
    if (isConnected)
    {
        // 取消所有待处理的 I/O 操作
        CancelIo(deviceHandle);
        isConnected = false;
    }

    WORD vendorId = (WORD)info[0].As<Napi::Number>().Uint32Value();
    WORD productId = (WORD)info[1].As<Napi::Number>().Uint32Value();

    std::cout << "Trying to connect to device with VID: 0x" << std::hex << vendorId
              << " PID: 0x" << productId << std::dec << std::endl;

    std::string devicePath = GetDevicePath(vendorId, productId);
    if (devicePath.empty())
    {
        return Napi::Boolean::New(env, false);
    }

    // 打开设备
    deviceHandle = CreateFileA(devicePath.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,  // 不共享
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,      //海沃佳的只能是Normal
                             NULL);

    if (deviceHandle == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to open device: " << GetLastError() << std::endl;
        return Napi::Boolean::New(env, false);
    }

    // 重置其他状态
    sendProgress = 0.0;
    isOperationInProgress = false;

    isConnected = true;
    std::cout << "Device connected successfully" << std::endl;
    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::Disconnect(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (isConnected)
    {
        if (deviceHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
        isConnected = false;
    }

    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::SendPlt(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!isConnected || deviceHandle == INVALID_HANDLE_VALUE)
    {
        Napi::Error::New(env, "Device not connected").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (info.Length() < 1 || !info[0].IsBuffer())
    {
        Napi::TypeError::New(env, "Expected buffer as argument").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    if (buffer.Length() == 0)
    {
        Napi::Error::New(env, "Empty buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 1. 取消所有待处理的 I/O 操作
    CancelIo(deviceHandle);
    Sleep(10);  // 给设备一些时间处理取消操作

    // 2. 写入数据
    DWORD bytesWritten = 0;
    BOOL writeResult = WriteFile(
        deviceHandle,
        buffer.Data(),
        static_cast<DWORD>(buffer.Length()),
        &bytesWritten,
        NULL  // 不使用 OVERLAPPED
    );

    if (!writeResult)
    {
        DWORD error = GetLastError();
        std::cout << "Write operation failed with error: " << error << std::endl;
        Napi::Error::New(env, "Failed to write data: " + std::to_string(error)).ThrowAsJavaScriptException();
        return env.Null();
    }

    std::cout << "Successfully wrote " << bytesWritten << " bytes" << std::endl;

    // 3. 等待设备处理命令
    Sleep(50);  // 给设备一些处理时间

    // 4. 读取响应
    const DWORD READ_BUFFER_SIZE = 1024;
    std::vector<uint8_t> readBuffer(READ_BUFFER_SIZE);
    DWORD bytesRead = 0;
    bool responseReceived = false;
    int readAttempts = 0;
    const int MAX_READ_ATTEMPTS = 5;
    DWORD startTime = GetTickCount();
    const DWORD TIMEOUT = 500;

    while (!responseReceived && readAttempts < MAX_READ_ATTEMPTS && (GetTickCount() - startTime) < TIMEOUT)
    {
        readAttempts++;
        
        // 使用同步读取
        BOOL readResult = ReadFile(
            deviceHandle,
            readBuffer.data(),
            READ_BUFFER_SIZE,
            &bytesRead,
            NULL  // 不使用 OVERLAPPED
        );

        if (!readResult)
        {
            DWORD error = GetLastError();
            if (error == ERROR_NO_DATA)
            {
                std::cout << "No data available, retrying..." << std::endl;
                Sleep(10);
            }
            else
            {
                std::cout << "Error while reading response: " << error << std::endl;
            }
        }
        else if (bytesRead > 0)
        {
            responseReceived = true;
            std::cout << "Received response of " << bytesRead << " bytes" << std::endl;
            break;
        }

        if (!responseReceived)
        {
            Sleep(10);
        }
    }

    // 5. 返回结果
    if (responseReceived && bytesRead > 0)
    {
        CancelIo(deviceHandle);
        Sleep(10);
        // 创建一个新的缓冲区，只包含实际接收到的数据
        std::vector<uint8_t> actualResponse(readBuffer.begin(), readBuffer.begin() + bytesRead);
        return Napi::Buffer<uint8_t>::Copy(env, actualResponse.data(), actualResponse.size());
    }

    std::cout << "No valid response received after " << MAX_READ_ATTEMPTS << " attempts" << std::endl;
    return env.Null();
}

Napi::Value UsbDevice::SendCmd(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    try {
        if (!isConnected || deviceHandle == INVALID_HANDLE_VALUE)
        {
            Napi::Error::New(env, "Device not connected").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (info.Length() < 1 || !info[0].IsBuffer())
        {
            Napi::TypeError::New(env, "Expected buffer as argument").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
        if (buffer.Length() == 0)
        {
            Napi::Error::New(env, "Empty buffer").ThrowAsJavaScriptException();
            return env.Null();
        }

        // 2. 写入数据
        DWORD bytesWritten = 0;
        BOOL writeResult = WriteFile(
            deviceHandle,
            buffer.Data(),
            static_cast<DWORD>(buffer.Length()),
            &bytesWritten,
            NULL  // 不使用 OVERLAPPED
        );

        if (!writeResult)
        {
            DWORD error = GetLastError();
            std::cout << "Write operation failed with error: " << error << std::endl;
            
            // 通过回调发送错误事件
            auto errorEvent = new std::pair<EventType, std::string>(
                EventType::ERR,
                "Failed to write data: " + std::to_string(error)
            );
            
            tsfn.BlockingCall(errorEvent, [](Napi::Env env, Napi::Function jsCallback, std::pair<EventType, std::string>* event) {
                Napi::String eventType = Napi::String::New(env, "ERROR");
                Napi::String message = Napi::String::New(env, event->second);
                jsCallback.Call({eventType, message});
                delete event;
            });
            
            Napi::Error::New(env, "Failed to write data: " + std::to_string(error)).ThrowAsJavaScriptException();
            return env.Null();
        }

        std::cout << "Successfully wrote " << bytesWritten << " bytes" << std::endl;

        // 3. 等待设备处理命令
        Sleep(10);  // 给设备一些处理时间

        // 4. 读取响应
        const DWORD READ_BUFFER_SIZE = 1024;
        std::vector<uint8_t> readBuffer(READ_BUFFER_SIZE);
        std::vector<uint8_t> completeResponse;  // 存储完整的响应数据
        DWORD bytesRead = 0;
        bool responseReceived = false;
        int readAttempts = 0;
        const int MAX_READ_ATTEMPTS = 3;  // 减少最大尝试次数
        DWORD startTime = GetTickCount();
        const DWORD TIMEOUT = 50;  // 减少超时时间到100ms

        while (!responseReceived && readAttempts < MAX_READ_ATTEMPTS && (GetTickCount() - startTime) < TIMEOUT)
        {
            readAttempts++;
            
            // 使用同步读取
            BOOL readResult = ReadFile(
                deviceHandle,
                readBuffer.data(),
                READ_BUFFER_SIZE,
                &bytesRead,
                NULL  // 不使用 OVERLAPPED
            );

            if (readResult && bytesRead > 0)
            {
                // 将读取到的数据添加到完整响应中
                completeResponse.insert(completeResponse.end(), readBuffer.begin(), readBuffer.begin() + bytesRead);
                std::cout << "Received " << bytesRead << " bytes, total: " << completeResponse.size() << " bytes" << std::endl;

                // 检查最后一个字符是否为分号
                if (!completeResponse.empty() && completeResponse.back() == ';')
                {
                    responseReceived = true;
                    break;
                }
            }
            else
            {
                DWORD error = GetLastError();
                if (error == ERROR_NO_DATA)
                {
                    if (!completeResponse.empty())
                    {
                        responseReceived = true;
                        break;
                    }
                }
                else
                {
                    std::cout << "Error while reading response: " << error << std::endl;
                }
            }

            if (!responseReceived)
            {
                std::cout << "No response received, retrying..." << std::endl;
                Sleep(5);  // 短暂等待后重试
            }
        }

        // 5. 通过回调返回结果
        if (responseReceived && !completeResponse.empty())
        {
            std::cout << "Total response size: " << completeResponse.size() << " bytes" << std::endl;
            
            // 创建一个新的缓冲区来存储响应数据
            auto responseBuffer = new std::vector<uint8_t>(completeResponse);
            
            // 通过回调发送数据
            tsfn.BlockingCall(responseBuffer, [](Napi::Env env, Napi::Function jsCallback, std::vector<uint8_t>* response) {
                // 创建事件类型字符串
                Napi::String eventType = Napi::String::New(env, "CMD_RESPONSE");
                
                // 创建 Buffer 对象
                Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, response->data(), response->size());
                
                // 调用 JavaScript 回调
                jsCallback.Call({eventType, buffer});
                
                // 清理
                delete response;
            });
        }
        else
        {
            std::cout << "No valid response received after " << MAX_READ_ATTEMPTS << " attempts" << std::endl;
            // 通过回调发送错误事件
            auto errorEvent = new std::pair<EventType, std::string>(
                EventType::ERR,
                "No valid response received after " + std::to_string(MAX_READ_ATTEMPTS) + " attempts"
            );
            
            tsfn.BlockingCall(errorEvent, [](Napi::Env env, Napi::Function jsCallback, std::pair<EventType, std::string>* event) {
                Napi::String eventType = Napi::String::New(env, "ERROR");
                Napi::String message = Napi::String::New(env, event->second);
                jsCallback.Call({eventType, message});
                delete event;
            });
        }

        return env.Null();
    }
    catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        
        // 通过回调发送错误事件
        auto errorEvent = new std::pair<EventType, std::string>(
            EventType::ERR,
            std::string("Exception: ") + e.what()
        );
        
        tsfn.BlockingCall(errorEvent, [](Napi::Env env, Napi::Function jsCallback, std::pair<EventType, std::string>* event) {
            Napi::String eventType = Napi::String::New(env, "ERROR");
            Napi::String message = Napi::String::New(env, event->second);
            jsCallback.Call({eventType, message});
            delete event;
        });
        
        return env.Null();
    }
}

Napi::Value UsbDevice::GetSendProgress(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    return Napi::Number::New(env, sendProgress);
}

LRESULT CALLBACK UsbDevice::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_DEVICECHANGE)
    {
        auto device = reinterpret_cast<UsbDevice *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (device && device->tsfn)
        {
            const char *eventType = nullptr;
            if (wParam == DBT_DEVICEARRIVAL)
            {
                eventType = "HOTPLUG";
            }
            else if (wParam == DBT_DEVICEREMOVECOMPLETE)
            {
                eventType = "HOTPLUG";
            }

            if (eventType)
            {
                DEV_BROADCAST_DEVICEINTERFACE_A *devInterface = (DEV_BROADCAST_DEVICEINTERFACE_A *)lParam;
                std::string devicePath = devInterface->dbcc_name;

                // 使用正则表达式提取VID和PID
                std::regex vidPidRegex("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})");
                std::smatch match;
                std::string vid = "Unknown";
                std::string pid = "Unknown";

                if (std::regex_search(devicePath, match, vidPidRegex) && match.size() == 3)
                {
                    vid = match.str(1);
                    pid = match.str(2);
                }

                // 创建一个结构来传递事件信息
                struct HotplugEvent
                {
                    std::string type;
                    std::string vid;
                    std::string pid;
                    std::string action;  // "Arrival" 或 "Remove"
                };

                auto event = new HotplugEvent{
                    eventType,
                    vid,
                    pid,
                    (wParam == DBT_DEVICEARRIVAL) ? "Arrival" : "Remove"
                };

                device->tsfn.BlockingCall(event, [](Napi::Env env, Napi::Function jsCallback, HotplugEvent *event)
                                          {
                    // 创建参数
                    Napi::String jsEventType = Napi::String::New(env, event->type);
                    Napi::Object jsData = Napi::Object::New(env);
                    jsData.Set("vid", Napi::String::New(env, event->vid));
                    jsData.Set("pid", Napi::String::New(env, event->pid));
                    jsData.Set("action", Napi::String::New(env, event->action));

                    // 调用JavaScript回调
                    jsCallback.Call({jsEventType, jsData});

                    // 清理
                    delete event; });
            }
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void UsbDevice::NotificationThreadProc()
{
    // 鍒涘缓闅愯棌绐楀彛鏉ユ帴鏀惰澶囬€氱煡
    WNDCLASSEXA wndClass = {sizeof(WNDCLASSEXA)};
    wndClass.lpfnWndProc = WindowProc;
    wndClass.lpszClassName = "UsbNotificationClass";
    RegisterClassExA(&wndClass);

    HWND hwnd = CreateWindowExA(0, "UsbNotificationClass", "UsbNotificationWindow",
                                0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    DEV_BROADCAST_DEVICEINTERFACE notificationFilter = {0};
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USBPRINT;

    deviceNotificationHandle = RegisterDeviceNotification(hwnd,
                                                          &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    MSG msg;
    while (!shouldStopNotification && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (deviceNotificationHandle)
    {
        UnregisterDeviceNotification(deviceNotificationHandle);
        deviceNotificationHandle = nullptr;
    }

    DestroyWindow(hwnd);
    UnregisterClassA("UsbNotificationClass", nullptr);
}

Napi::Value UsbDevice::StartHotplugMonitor(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction())
    {
        Napi::TypeError::New(env, "Expected callback function")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    tsfn = Napi::ThreadSafeFunction::New(
        env,
        info[0].As<Napi::Function>(),
        "HotplugCallback",
        0,
        1);

    shouldStopNotification = false;
    notificationThread = std::thread(&UsbDevice::NotificationThreadProc, this);

    return Napi::Boolean::New(env, true);
}

Napi::Value UsbDevice::StopHotplugMonitor(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    try
    {
        // 检查线程是否在运行
        if (!notificationThread.joinable())
        {
            return Napi::Boolean::New(env, true);
        }

        // 安全地停止线程
        shouldStopNotification = true;

        // 等待线程结束
        if (notificationThread.joinable())
        {
            notificationThread.join();
        }

        // 清理通知句柄
        if (deviceNotificationHandle)
        {
            UnregisterDeviceNotification(deviceNotificationHandle);
            deviceNotificationHandle = nullptr;
        }

        return Napi::Boolean::New(env, true);
    }
    catch (const std::exception &e)
    {
        Napi::Error::New(env, std::string("Error in StopHotplugMonitor: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

void UsbDevice::ProcessSendQueue()
{
    // 简单实现
}

// 初始化导出函数
Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    return UsbDevice::Init(env, exports);
}

NODE_API_MODULE(usb_addon, Init)

