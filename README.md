# USB Node.js Addon

这是一个用于 USB 设备通信的 Node.js 原生模块，支持以下功能：
- USB 设备连接和断开
- 数据发送
- 发送进度监控
- USB 设备热插拔监控

## 系统要求

- Node.js 14.x 或更高版本
- libusb 1.0
- Windows 或 Linux 操作系统
- 用于编译的构建工具（Windows: Visual Studio，Linux: GCC）

## 安装

### Windows

1. 安装 libusb：
   - 下载并安装 [libusb](https://github.com/libusb/libusb/releases)
   - 设置环境变量 `LIBUSB_DIR` 指向 libusb 安装目录

2. 安装模块：
```bash
npm install
```

### Linux

1. 安装 libusb：
```bash
sudo apt-get install libusb-1.0-0-dev  # Ubuntu/Debian
# 或
sudo yum install libusb1-devel         # CentOS/RHEL
```

2. 安装模块：
```bash
npm install
```

## 使用示例

```javascript
const USB = require('./');

// 创建 USB 实例
const usb = new USB();

// 连接设备（使用供应商 ID 和产品 ID）
usb.connect(0x0483, 0x5740);

// 发送数据
const data = Buffer.from([0x01, 0x02, 0x03, 0x04]);
usb.sendData(data);

// 获取发送进度
console.log(usb.getSendProgress());

// 监控热插拔
usb.startHotplugMonitor((isAttached) => {
    console.log(isAttached ? '设备已连接' : '设备已断开');
});

// 停止热插拔监控
usb.stopHotplugMonitor();

// 断开连接
usb.disconnect();
```

## API 文档

### `new USB()`
创建新的 USB 实例。

### `connect(vendorId, productId)`
- `vendorId`: number - USB 设备供应商 ID
- `productId`: number - USB 设备产品 ID
- 返回: boolean - 连接是否成功

### `disconnect()`
- 返回: boolean - 断开连接是否成功

### `sendData(data)`
- `data`: Buffer | Uint8Array - 要发送的数据
- 返回: boolean - 发送是否成功

### `getSendProgress()`
- 返回: number - 当前发送进度（0-1 之间的数值）

### `startHotplugMonitor(callback)`
- `callback`: (isAttached: boolean) => void - 热插拔事件回调函数
- 返回: boolean - 监控是否成功启动

### `stopHotplugMonitor()`
- 返回: boolean - 监控是否成功停止

## 许可证

ISC 