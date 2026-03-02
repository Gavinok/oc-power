import Toybox.Application;
import Toybox.BluetoothLowEnergy;
import Toybox.Lang;
import Toybox.WatchUi;

class PaddlePowerApp extends Application.AppBase {

    // Shared power metrics (read by PaddlePowerView.compute())
    var currentPower as Lang.Number  = 0;
    var totalPower   as Lang.Long    = 0l;
    var readingCount as Lang.Number  = 0;
    var power3sBuffer as Lang.Array  = [0, 0, 0] as Lang.Array<Lang.Number>;
    var bufferIndex  as Lang.Number  = 0;
    var bleStatus    as Lang.String  = "Scanning";

    private var _bleDelegate as PowerBleDelegate?;

    function initialize() {
        AppBase.initialize();
    }

    function onStart(state as Lang.Dictionary?) as Void {
        // Register our BLE profile so the stack knows which UUIDs we care about
        var powerServiceUuid = BluetoothLowEnergy.stringToUuid(
            "00001818-0000-1000-8000-00805F9B34FB"
        );
        var powerCharUuid = BluetoothLowEnergy.stringToUuid(
            "00002A63-0000-1000-8000-00805F9B34FB"
        );

        var charDef = {
            :uuid        => powerCharUuid,
            :descriptors => [BluetoothLowEnergy.cccdUuid()]
        };
        var profileDef = {
            :uuid            => powerServiceUuid,
            :characteristics => [charDef]
        };
        BluetoothLowEnergy.registerProfile(profileDef);

        // Create the delegate and start scanning
        _bleDelegate = new PowerBleDelegate(self);
        BluetoothLowEnergy.setDelegate(_bleDelegate);
        _bleDelegate.startScan();
    }

    function onStop(state as Lang.Dictionary?) as Void {
        if (_bleDelegate != null) {
            (_bleDelegate as PowerBleDelegate).stopScan();
        }
    }

    function getInitialView() as [WatchUi.Views] or [WatchUi.Views, WatchUi.InputDelegates] {
        return [new PaddlePowerView()];
    }

    // Called by PowerBleDelegate when a new power reading arrives
    function onPowerReading(watts as Lang.Number) as Void {
        currentPower = watts;

        // Session average accumulation
        totalPower   += watts;
        readingCount += 1;

        // 3-second rolling buffer (circular)
        power3sBuffer[bufferIndex] = watts;
        bufferIndex = (bufferIndex + 1) % 3;
    }
}

// Global convenience accessor (matches Connect IQ idiom)
function getApp() as PaddlePowerApp {
    return Application.getApp() as PaddlePowerApp;
}
