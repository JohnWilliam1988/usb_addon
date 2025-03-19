const addon = require('./build/Release/usb_addon.node');

// 创建 USB 设备实例
const device = new addon.UsbDevice();

// 定义 VID 和 PID - 可以设置为 0, 0 来匹配第一个找到的打印机
const VID = 0x0483;  // 0 表示匹配任何 VID
const PID = 0x5448;  // 0 表示匹配任何 PID  

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
        device.startHotplugMonitor(hotplugCallback);
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
            
            // 发送测试数据 - ESC/POS 打印机指令示例
            const testData = Buffer.from(";:H A L0 ECN P0;U51,1210;D53,1181;D65,1125;D83,1072;D109,1023;D140,978;D179,934;D221,899;D267,869;D306,849;D347,834;D390,823;D431,817;D809,815;D811,431;D817,390;D828,347;D843,306;D862,267;D892,221;D928,179;D971,140;D1017,109;D1066,83;D1119,65;D1174,53;D1233,49;D1291,53;D1346,65;D1399,83;D1449,109;D1494,140;D1537,179;D1573,221;D1603,267;D1622,306;D1637,347;D1648,390;D1654,431;D1656,815;D2038,817;D2079,823;D2122,834;D2163,849;D2202,869;D2249,899;D2290,934;D2330,978;D2361,1023;D2386,1072;D2405,1125;D2416,1181;D2420,1239;D2416,1297;D2405,1353;D2386,1405;D2361,1455;D2330,1500;D2290,1544;D2249,1579;D2202,1609;D2163,1628;D2122,1643;D2079,1654;D2038,1660;D1656,1662;D1654,2038;D1648,2079;D1637,2122;D1622,2163;D1603,2202;D1573,2249;D1537,2290;D1494,2330;D1449,2361;D1399,2386;D1346,2405;D1291,2416;D1233,2420;D1174,2416;D1119,2405;D1066,2386;D1017,2361;D971,2330;D928,2290;D892,2249;D862,2202;D843,2163;D828,2122;D817,2079;D811,2038;D809,1662;D431,1660;D390,1654;D347,1643;D306,1628;D267,1609;D221,1579;D179,1544;D140,1500;D109,1455;D83,1405;D65,1353;D53,1297;D49,1239;D51,1210;U0,0;@;@");
            
            console.log('发送数据到打印机...');
            
           const bytesWritten = device.sendData(testData);
           console.log(`成功发送 ${bytesWritten} 字节`);


            // for (let i = 0; i < 10; i++) {
            //     // 发送测试数据并接收响应
                
            //     sleep(2000);
            // }

            const testData1 = Buffer.from('USBS;');
            setInterval(() => {
                
                console.log('发送数据到打印机并等待响应...');
                try {
                    const responseData =  device.sendDataWithResponse(testData1);
                    console.log('接收到的响应:', responseData.toString());
                } catch (error) {
                    console.log('发送数据失败:', error);
                    console.error('发送数据失败:', error);
                }
            }, 2200);
            
            // 断开连接
            //device.disconnect();
            // console.log('设备已断开连接');
        } else {
            console.log('未找到打印机设备');
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