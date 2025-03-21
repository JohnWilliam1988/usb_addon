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


// 读取固件版本：GETVER;
// 读取SN：USERDEF_RD;
// 写SN：  USERDEF_WT:XXXXXXXXXXXXXXXXXXXXXXXX;
// 读当前速度与刀压：GETVF;
// 设置速度：SETSPEED:VVV;(范围：10-1000）
// 设置刀压: SETLFORCE:FFF;(左刀头），  SETRFORCE:FFF;(右刀头）   （刀压范围：10-500）
// 机器复位：RESET;
// 查询机器状态：EHLO;  空闲返回ONLINE.    工作中不返回
// 获取当前校准值：GETLAS;
// 左右微调指令:SETLASX:XX;   单位是mm,
// 前后微调指令SETLASY:YY;    单位是mm
// 设置左刀：P0;
// 设置右刀：P1;
// 设置巡边黑框： TB26,0,YYY,XXX; 其中YYY是前后长度，XXX是左右长度。
// 获取蓝牙名称：GETBTN; 
// 每天指令间隔150ms以上

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

      const isReady = "USBS;"
      const isReadyData = Buffer.from(isReady);
      try {
        const status = device.sendDataWithResponse(isReadyData);
        if (status.toString() === "S0") {
          console.log("打印机准备就绪")
          const testData = Buffer.from(";:H A L0 ECN U0,0;U51,1210;D53,1181;D65,1125;D83,1072;D109,1023;D140,978;D179,934;D221,899;D267,869;D306,849;D347,834;D390,823;D431,817;D809,815;D811,431;D817,390;D828,347;D843,306;D862,267;D892,221;D928,179;D971,140;D1017,109;D1066,83;D1119,65;D1174,53;D1233,49;D1291,53;D1346,65;D1399,83;D1449,109;D1494,140;D1537,179;D1573,221;D1603,267;D1622,306;D1637,347;D1648,390;D1654,431;D1656,815;D2038,817;D2079,823;D2122,834;D2163,849;D2202,869;D2249,899;D2290,934;D2330,978;D2361,1023;D2386,1072;D2405,1125;D2416,1181;D2420,1239;D2416,1297;D2405,1353;D2386,1405;D2361,1455;D2330,1500;D2290,1544;D2249,1579;D2202,1609;D2163,1628;D2122,1643;D2079,1654;D2038,1660;D1656,1662;D1654,2038;D1648,2079;D1637,2122;D1622,2163;D1603,2202;D1573,2249;D1537,2290;D1494,2330;D1449,2361;D1399,2386;D1346,2405;D1291,2416;D1233,2420;D1174,2416;D1119,2405;D1066,2386;D1017,2361;D971,2330;D928,2290;D892,2249;D862,2202;D843,2163;D828,2122;D817,2079;D811,2038;D809,1662;D431,1660;D390,1654;D347,1643;D306,1628;D267,1609;D221,1579;D179,1544;D140,1500;D109,1455;D83,1405;D65,1353;D53,1297;D49,1239;D51,1210;U0,0;@;@");

          console.log('发送数据到打印机...');

          const bytesWritten = device.sendData(testData);
          console.log(`成功发送 ${bytesWritten} 字节`);



          const testData1 = Buffer.from('USBS;');
          // const testData1 = Buffer.from('BD:10;');
          setTimeout(() => {
            let timer = setInterval(() => {

              console.log('发送数据到打印机并等待响应...');
              try {
                const responseData = device.sendDataWithResponse(testData1);
                console.log('接收到的响应:', responseData.toString());
              } catch (error) {
                console.log('发送数据失败:', error);
                console.error('发送数据失败:', error);
                // clearInterval(timer);    
              }
            }, 2000);
          }, 3000);
        } else {
          console.log('打印机未准备就绪');
        }
      } catch (error) {
        console.log('发送数据失败:', error);
        console.error('发送数据失败:', error);
      }



    } else {
      console.log('未找到打印机设备');
    }
  } catch (error) {
    console.error('发生错误: 1111111111111111', error);
    // if (device) {
    //     try {
    //         device.disconnect();
    //     } catch (e) {}
    // }
  }
}


testDevice();


