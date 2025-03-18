const { UsbDevice } = require('bindings')('usb_addon');

class USB {
    constructor() {
        this.device = new UsbDevice();
        this._isConnected = false;
        this._isMonitoring = false;
        this._hotplugCallback = null;
    }

    connect(vendorId, productId) {
        if (this._isConnected) {
            throw new Error('Device already connected');
        }
        
        const result = this.device.connect(vendorId, productId);
        this._isConnected = result;
        return result;
    }

    disconnect() {
        if (!this._isConnected) {
            return false;
        }
        
        const result = this.device.disconnect();
        this._isConnected = !result;
        return result;
    }

    sendData(data) {
        if (!this._isConnected) {
            throw new Error('Device not connected');
        }

        if (!(data instanceof Buffer || data instanceof Uint8Array)) {
            throw new TypeError('Data must be Buffer or Uint8Array');
        }

        return this.device.sendData(data);
    }

    getSendProgress() {
        return this.device.getSendProgress();
    }

    startHotplugMonitor(callback) {
        if (this._isMonitoring) {
            throw new Error('Hotplug monitoring already started');
        }

        if (typeof callback !== 'function') {
            throw new TypeError('Callback must be a function');
        }

        this._hotplugCallback = callback;
        this._isMonitoring = true;
        return this.device.startHotplugMonitor(callback);
    }

    stopHotplugMonitor() {
        if (!this._isMonitoring) {
            return false;
        }

        const result = this.device.stopHotplugMonitor();
        this._isMonitoring = !result;
        this._hotplugCallback = null;
        return result;
    }
}

module.exports = USB; 