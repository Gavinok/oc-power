import Toybox.BluetoothLowEnergy;
import Toybox.Lang;

// BLE state machine states
enum BleState {
    STATE_SCANNING,
    STATE_PAIRING,
    STATE_CONNECTED,
    STATE_SUBSCRIBING,
    STATE_RECEIVING
}

class PowerBleDelegate extends BluetoothLowEnergy.BleDelegate {

    private var _app          as PaddlePowerApp;
    private var _state        as BleState = STATE_SCANNING;
    private var _powerChar    as BluetoothLowEnergy.Characteristic?;

    private var _powerServiceUuid as BluetoothLowEnergy.Uuid;
    private var _powerCharUuid    as BluetoothLowEnergy.Uuid;

    function initialize(app as PaddlePowerApp) {
        BleDelegate.initialize();
        _app = app;
        _powerServiceUuid = BluetoothLowEnergy.stringToUuid(
            "00001818-0000-1000-8000-00805F9B34FB"
        );
        _powerCharUuid = BluetoothLowEnergy.stringToUuid(
            "00002A63-0000-1000-8000-00805F9B34FB"
        );
    }

    function startScan() as Void {
        _state = STATE_SCANNING;
        _app.bleStatus = "Scanning";
        BluetoothLowEnergy.setScanState(BluetoothLowEnergy.SCAN_STATE_SCANNING);
    }

    function stopScan() as Void {
        BluetoothLowEnergy.setScanState(BluetoothLowEnergy.SCAN_STATE_OFF);
    }

    // ------------------------------------------------------------------
    // Scanning — called when advertising packets are received
    // ------------------------------------------------------------------
    function onScanResults(scanResults as BluetoothLowEnergy.Iterator) as Void {
        if (_state != STATE_SCANNING) {
            return;
        }

        var result = scanResults.next() as BluetoothLowEnergy.ScanResult?;
        while (result != null) {
            var serviceUuids = result.getServiceUuids();
            if (serviceUuids != null) {
                var uuid = serviceUuids.next() as BluetoothLowEnergy.Uuid?;
                while (uuid != null) {
                    if (uuid.equals(_powerServiceUuid)) {
                        _state = STATE_PAIRING;
                        _app.bleStatus = "Pairing";
                        stopScan();
                        BluetoothLowEnergy.pairDevice(result);
                        return;
                    }
                    uuid = serviceUuids.next() as BluetoothLowEnergy.Uuid?;
                }
            }
            result = scanResults.next() as BluetoothLowEnergy.ScanResult?;
        }
    }

    // ------------------------------------------------------------------
    // Connection state changes — fired after pairDevice() succeeds
    // and on later disconnections
    // ------------------------------------------------------------------
    function onConnectedStateChanged(device as BluetoothLowEnergy.Device,
                                     state  as BluetoothLowEnergy.ConnectionState) as Void {
        if (state == BluetoothLowEnergy.CONNECTION_STATE_CONNECTED) {
            _state = STATE_CONNECTED;
            _app.bleStatus = "Connected";
            _discoverService(device);
        } else {
            // Disconnected — reset and scan again
            _powerChar = null;
            startScan();
        }
    }

    // ------------------------------------------------------------------
    // Service / characteristic discovery
    // ------------------------------------------------------------------
    private function _discoverService(device as BluetoothLowEnergy.Device) as Void {
        var service = device.getService(_powerServiceUuid);
        if (service == null) {
            startScan();
            return;
        }

        var characteristic = service.getCharacteristic(_powerCharUuid);
        if (characteristic == null) {
            startScan();
            return;
        }

        _powerChar = characteristic;
        _subscribeToPowerChar();
    }

    private function _subscribeToPowerChar() as Void {
        var ch = _powerChar;
        if (ch == null) {
            return;
        }

        var cccd = ch.getDescriptor(BluetoothLowEnergy.cccdUuid());
        if (cccd == null) {
            return;
        }

        _state = STATE_SUBSCRIBING;
        _app.bleStatus = "Subscribing";
        // Write 0x0001 to enable notifications
        cccd.requestWrite([0x01, 0x00]b);
    }

    // ------------------------------------------------------------------
    // Descriptor write result
    // ------------------------------------------------------------------
    function onDescriptorWrite(descriptor as BluetoothLowEnergy.Descriptor,
                               status     as BluetoothLowEnergy.Status) as Void {
        if (status == BluetoothLowEnergy.STATUS_SUCCESS) {
            _state = STATE_RECEIVING;
            _app.bleStatus = "Connected";
        } else {
            startScan();
        }
    }

    // ------------------------------------------------------------------
    // Incoming notifications — parse Cycling Power Measurement (0x2A63)
    //
    // Packet layout (all little-endian):
    //   [0-1]  flags         (uint16) — ignored
    //   [2-3]  instantaneous power (sint16, Watts)
    // ------------------------------------------------------------------
    function onCharacteristicChanged(characteristic as BluetoothLowEnergy.Characteristic,
                                     value          as Lang.ByteArray) as Void {
        if (value.size() < 4) {
            return;
        }

        // sint16 little-endian at bytes 2-3
        var raw = ((value[3] & 0xFF) << 8) | (value[2] & 0xFF);
        // Sign-extend from 16 bits
        if (raw > 0x7FFF) {
            raw = raw - 0x10000;
        }

        _app.onPowerReading(raw);

        // Crank revolution data — always present (flag 0x0020 always set by ESP32)
        if (value.size() >= 8) {
            var cumRevs = ((value[5] & 0xFF) << 8) | (value[4] & 0xFF);
            var evtTime = ((value[7] & 0xFF) << 8) | (value[6] & 0xFF);
            _app.onCrankReading(cumRevs, evtTime);
        }
    }
}
