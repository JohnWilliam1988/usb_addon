const addon = require('./build/Release/usb_addon.node');

// 创建 USB 设备实例
const device = new addon.UsbDevice();

// 定义 VID 和 PID - 可以设置为 0, 0 来匹配第一个找到的打印机
const VID = 0x0483;  // 0 表示匹配任何 VID
const PID = 0x5750;  // 0 表示匹配任何 PID  
// const PID = 0x5448;  // 0 表示匹配任何 PID  


const usbIdList = [
    {
      name: "HWJ",
      idVendor: 0x0483,
      idProduct: 0x5750,
    },
    {
      name: "GNS",
      idVendor: 0x0483,
      idProduct: 0x5448,
    },
  ];


 
let isMonitoringStarted = false;


function interpretAsHex(decimalValue) {
    // 将十进制值解释为十六进制数，再转回十进制
    // 例如: 482 -> "0x482" -> 1154
    let hexString = "0x" + decimalValue;
    return parseInt(hexString, 16);
}
// 热插拔回调函数
function hotplugCallback(eventType, vid, pid) {
    const now = new Date().toISOString();
    if (eventType === 'Arrival') {
        console.log(`[${now}] 设备已连接: VID=${vid}, PID=${pid}`);
    } else if (eventType === 'Remove') {
        console.log(`[${now}] 设备已断开: VID=${vid}, PID=${pid}`);
    }
}

// 启动热插拔监控
function startMonitoring() {
    try {
        console.log('启动热插拔监控...');
        device.startHotplugMonitor((eventType, data) => {
            switch (eventType) {
                case "HOTPLUG":
                    console.log("设备热插拔事件:", data);
                    break;
                case "CMD_RESPONSE":
                    console.log("命令响应:", data.toString());
                    break;
                case "ERROR":
                    console.error("错误:", data);
                    break;
            }
        });
        console.log('热插拔监控已启动，请插入或拔出 USB 打印机');
        
        // 设置一个信号处理器来捕获 Ctrl+C
        process.on('SIGINT', () => {
            cleanup();
            process.exit();
        });
        
        // 保持程序运行
        console.log('按 Ctrl+C 停止监控');
    } catch (error) {
        console.error('启动热插拔监控失败:', error);
    }
}

async function testDevice() {
    try {
        console.log('尝试连接 USB 打印机...');
        
        startMonitoring();
        // 连接设备
        const connected = device.connect(VID, PID);
        if (connected) {
            // 使用 async/await 来确保命令按顺序执行
            try {
                // 发送 RSVER 命令
                console.log("发送数据 RSVER;");
                const cmd = Buffer.from("RSVER;");
                device.sendCmd(cmd);


                // 发送 RPID 命令
                console.log("发送数据 RPID;");
                const data2 = Buffer.from("RPID;");
                device.sendCmd(data2);

                // 开始循环发送 BD:36 命令
                while (true) {
                    console.log("发送数据 BD:36;");
                    const data3 = Buffer.from("BD:36;");
                    device.sendCmd(data3);
                    
                    // 等待一段时间再发送下一条命令
                    await new Promise(resolve => setTimeout(resolve, 1000));
                }
            } catch (error) {
                console.error("发送命令时出错:", error);
            }
        }
    } catch (error) {
        console.error('发生错误:', error);
        if (device) {
            try {
                device.disconnect();
            } catch (e) {}
        }
    }
}

// 清理函数
async function cleanup() {
    try {
        if (device) {
            // 停止热插拔监控
            if (isMonitoringStarted) {
                try {
                    device.stopHotplugMonitor();
                    isMonitoringStarted = false;
                    console.log('热插拔监控已停止');
                } catch (e) {
                    console.warn('停止热插拔监控时发生错误:', e);
                }
            }

            // 断开连接
            try {
                device.disconnect();
                console.log('设备已断开连接');
            } catch (e) {
                console.warn('断开连接时发生错误:', e);
            }
        }
    } catch (error) {
        console.error('清理过程中发生错误:', error);
    }
}

// 处理程序退出
process.on('SIGINT', async () => {
    console.log('\n正在退出程序...');
    await cleanup();
    process.exit(0);
});

// 处理未捕获的异常
process.on('uncaughtException', async (error) => {
    console.error('未捕获的异常:', error);
    await cleanup();
    process.exit(1);
});

// 运行测试
console.log('开始USB测试...');
testDevice().catch(console.error);

// 定期发送数据的示例
function startPeriodicSend() {
    const interval = 1000; // 每秒发送一次
    const data = Buffer.from([0xAA, 0xBB, 0xCC, 0xDD]); // 示例数据

    setInterval(() => {
        if (device.isConnected) {
            console.log('发送周期数据...');
            device.sendData(data);
        }
    }, interval);
}

// 如果需要定期发送数据，取消下面这行的注释
// startPeriodicSend();

// 错误处理示例
function handleError(error) {
    console.error('发生错误:', error);
    cleanup();
}

// 使用示例：发送自定义数据
function sendCustomData(data) {
    try {
        if (!Buffer.isBuffer(data)) {
            data = Buffer.from(data);
        }
        device.sendData(data);
        console.log('自定义数据已发送');
    } catch (error) {
        handleError(error);
    }
}

// 使用示例：
// sendCustomData([0x01, 0x02, 0x03, 0x04]);
// 或
// sendCustomData(Buffer.from('Hello, USB!', 'utf8')); 