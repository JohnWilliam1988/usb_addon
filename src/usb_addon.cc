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

    Napi::Function func = DefineClass(env, "UsbDevice", {InstanceMethod("connect", &UsbDevice::Connect), InstanceMethod("disconnect", &UsbDevice::Disconnect), InstanceMethod("sendData", &UsbDevice::SendData), InstanceMethod("sendDataWithResponse", &UsbDevice::SendDataWithResponse), InstanceMethod("getSendProgress", &UsbDevice::GetSendProgress), InstanceMethod("startHotplugMonitor", &UsbDevice::StartHotplugMonitor), InstanceMethod("stopHotplugMonitor", &UsbDevice::StopHotplugMonitor)});

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

Napi::Value UsbDevice::SendData(const Napi::CallbackInfo &info)
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
    Sleep(10);

    // 创建写入事件
    HANDLE writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!writeEvent)
    {
        Napi::Error::New(env, "Failed to create write event").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 初始化 OVERLAPPED 结构
    OVERLAPPED osWrite = {0};
    osWrite.hEvent = writeEvent;
    osWrite.Offset = 0;
    osWrite.OffsetHigh = 0;

    DWORD bytesWritten = 0;
    BOOL writeResult = WriteFile(
        deviceHandle,
        buffer.Data(),
        static_cast<DWORD>(buffer.Length()),
        &bytesWritten,
        NULL);

    if (!writeResult)
    {
        DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING)
        {
            // 等待写入完成
            if (WaitForSingleObject(writeEvent, 5000) == WAIT_TIMEOUT)
            {
                CancelIo(deviceHandle);
                CloseHandle(writeEvent);
                Napi::Error::New(env, "Write operation timed out").ThrowAsJavaScriptException();
                return env.Null();
            }

            // 获取写入结果
            if (!GetOverlappedResult(deviceHandle, &osWrite, &bytesWritten, TRUE))
            {
                DWORD finalError = GetLastError();
                CloseHandle(writeEvent);
                Napi::Error::New(env, "Failed to complete write operation: " + std::to_string(finalError)).ThrowAsJavaScriptException();
                return env.Null();
            }
        }
        else
        {
            CloseHandle(writeEvent);
            Napi::Error::New(env, "Failed to write data: " + std::to_string(error)).ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    CloseHandle(writeEvent);

    std::cout << "Successfully wrote " << bytesWritten << " bytes" << std::endl;
    return Napi::Number::New(env, bytesWritten);
}

Napi::Value UsbDevice::SendDataWithResponse(const Napi::CallbackInfo &info)
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

        // 1. 取消所有待处理的 I/O 操作
        CancelIo(deviceHandle);

        // 2. 清空设备缓冲区
        const DWORD CLEAR_BUFFER_SIZE = 1024;
        std::vector<uint8_t> clearBuffer(CLEAR_BUFFER_SIZE);
        HANDLE clearEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (clearEvent) {
            bool keepClearing = true;
            int clearAttempts = 0;
            const int MAX_CLEAR_ATTEMPTS = 5;

            while (keepClearing && clearAttempts < MAX_CLEAR_ATTEMPTS) {
                OVERLAPPED clearOverlapped = {0};
                clearOverlapped.hEvent = clearEvent;
                ResetEvent(clearEvent);

                DWORD bytesRead = 0;
                BOOL readResult = ReadFile(
                    deviceHandle,
                    clearBuffer.data(),
                    CLEAR_BUFFER_SIZE,
                    &bytesRead,
                    &clearOverlapped);

                if (!readResult && GetLastError() != ERROR_IO_PENDING) {
                    break;
                }

                if (WaitForSingleObject(clearEvent, 50) != WAIT_OBJECT_0) {
                    break;
                }

                if (!GetOverlappedResult(deviceHandle, &clearOverlapped, &bytesRead, FALSE) || bytesRead == 0) {
                    keepClearing = false;
                }

                clearAttempts++;
                std::cout << "Cleared " << bytesRead << " bytes from buffer" << std::endl;
            }
            CloseHandle(clearEvent);
        }

        // 3. 创建写入和读取事件
        HANDLE writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        HANDLE readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!writeEvent || !readEvent) {
            if (writeEvent) CloseHandle(writeEvent);
            if (readEvent) CloseHandle(readEvent);
            return env.Null();
        }

        // 4. 写入数据
        OVERLAPPED writeOverlapped = {0};
        writeOverlapped.hEvent = writeEvent;

        std::cout << "Writing data: ";
        for (size_t i = 0; i < buffer.Length(); i++) {
            printf("%02X ", buffer.Data()[i]);
        }
        std::cout << std::endl;

        DWORD bytesWritten = 0;
        ResetEvent(writeEvent);
        BOOL writeResult = WriteFile(
            deviceHandle,
            buffer.Data(),
            static_cast<DWORD>(buffer.Length()),
            &bytesWritten,
            &writeOverlapped);

        if (!writeResult && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(writeEvent);
            CloseHandle(readEvent);
            return env.Null();
        }

        // 5. 等待写入完成
        if (WaitForSingleObject(writeEvent, 500) != WAIT_OBJECT_0) {
            CancelIo(deviceHandle);
            CloseHandle(writeEvent);
            CloseHandle(readEvent);
            return env.Null();
        }

        // 6. 等待设备处理命令
        Sleep(50);

        // 7. 读取响应
        const DWORD READ_BUFFER_SIZE = 1024;
        std::vector<uint8_t> readBuffer(READ_BUFFER_SIZE, 0);
        std::vector<uint8_t> responseBuffer;
        bool responseReceived = false;
        int readAttempts = 0;
        const int MAX_READ_ATTEMPTS = 3;

        while (!responseReceived && readAttempts < MAX_READ_ATTEMPTS) {
            readAttempts++;
            
            OVERLAPPED readOverlapped = {0};
            readOverlapped.hEvent = readEvent;
            ResetEvent(readEvent);

            DWORD bytesRead = 0;
            BOOL readResult = ReadFile(
                deviceHandle,
                readBuffer.data(),
                READ_BUFFER_SIZE,
                &bytesRead,
                &readOverlapped);

            if (!readResult && GetLastError() != ERROR_IO_PENDING) { 
                continue;
            }

            // 等待读取完成
            DWORD waitResult = WaitForSingleObject(readEvent, 500);
            if (waitResult == WAIT_OBJECT_0) {
                if (GetOverlappedResult(deviceHandle, &readOverlapped, &bytesRead, FALSE)) {
                    if (bytesRead > 0) {
                        std::cout << "Read " << bytesRead << " bytes: ";
                        for (DWORD i = 0; i < bytesRead; i++) {
                            printf("%02X ", readBuffer[i]);
                        }
                        std::cout << std::endl;

                        // 检查是否为有效响应
                        bool isValidResponse = false;
                        for (DWORD i = 0; i < bytesRead; i++) {
                            if (readBuffer[i] != 0) {
                                isValidResponse = true;
                                break;
                            }
                        }

                        if (isValidResponse) {
                            responseBuffer.insert(responseBuffer.end(), 
                                               readBuffer.begin(), 
                                               readBuffer.begin() + bytesRead);
                            responseReceived = true;
                        }
                    }
                }
            }

            if (!responseReceived) {
                Sleep(20);
            }
        }

        // 8. 清理资源
        CloseHandle(writeEvent);
        CloseHandle(readEvent);

        // 9. 返回结果
        if (responseReceived && !responseBuffer.empty()) {
            return Napi::Buffer<uint8_t>::Copy(env, responseBuffer.data(), responseBuffer.size());
        }

        return env.Null();
    }
    catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
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
                eventType = "Arrival";
            }
            else if (wParam == DBT_DEVICEREMOVECOMPLETE)
            {
                eventType = "Remove";
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
                };

                auto event = new HotplugEvent{eventType, vid, pid};

                device->tsfn.BlockingCall(event, [](Napi::Env env, Napi::Function jsCallback, HotplugEvent *event)
                                          {
                    // 创建参数
                    Napi::String jsEventType = Napi::String::New(env, event->type);
                    Napi::String jsVid = Napi::String::New(env, event->vid);
                    Napi::String jsPid = Napi::String::New(env, event->pid);

                    // 调用JavaScript回调
                    jsCallback.Call({jsEventType, jsVid, jsPid});

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
