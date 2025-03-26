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


async function testDevice() {
  try {
    console.log('尝试连接 USB 打印机...');
    // 连接设备

    let connected;

    for (let i = 0; i < usbIdList.length; i++) {
      const usbId = usbIdList[i];

      connected = device.connect(usbId.idVendor, usbId.idProduct);

      // console.log(connected,"connected")
      console.log("连接结果:", connected);

      if (connected) {

        break; // 可以直接退出循环
      }
    }

    // const connected = device.connect(VID, PID);

    if (connected) {
      let pltString = "TB25,2802,4156;CT1;                                                                                                                                                                                                                                                                                            IN;PU0,2155;PD0,979;PD0,979;PD0,980;PD0,981;PD0,983;PD0,985;PD0,987;PD0,990;PD0,992;PD0,995;PD1976,995;PD1976,998;PD1975,1000;PD1974,1003;PD1972,1005;PD1970,1007;PD1968,1009;PD1965,1010;PD1963,1011;PD1960,1011;PD1960,3331;PD1957,3331;PD1955,3330;PD1952,3329;PD1950,3327;PD1948,3325;PD1946,3323;PD1945,3320;PD1944,3318;PD1944,3315;PD0,3315;PD0,3312;PD0,3310;PD0,3307;PD0,3305;PD0,3303;PD0,3301;PD0,3300;PD0,3299;PD0,3299;PD0,2155;PD0,2139;PU0,0;PG;@"
      const pltStringData = Buffer.from(pltString);
      try {
        const bytes = device.sendData(pltStringData);
        console.log("bytes", bytes.toString())
      } catch (error) {
        console.log('发送数据失败:', error);
        console.error('发送数据失败:', error);
      }
    } else {
      console.log('未找到打印机设备');
    }
  } catch (error) {
    console.error('发生错误: 1111111111111111', error);
    if (device) {
        try {
            device.disconnect();
        } catch (e) {}
    }
  }
}


testDevice();


