import Toybox.Application;
import Toybox.BluetoothLowEnergy;
import Toybox.Lang;
import Toybox.System;
import Toybox.WatchUi;

class PaddlePowerApp extends Application.AppBase {

    // Shared power metrics (read by PaddlePowerView.compute())
    var currentPower  as Lang.Number = 0;
    var strokeRate    as Lang.Number = 0;
    var strokeCount   as Lang.Number = 0;
    var _prevRevs     as Lang.Number = -1;
    var _prevEvtTime  as Lang.Number = -1;
    // Time-stamped history for a true 3-second rolling average.
    // Each entry is [timestamp_ms as Number, watts as Number].
    var power3sHistory as Lang.Array = [] as Lang.Array;
    // Raw watts ring buffer for debug bar chart (max 60 entries = ~6 s at 10 Hz)
    var debugHistory  as Lang.Array = [] as Lang.Array;
    var bleStatus     as Lang.String = "Scanning";

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

    // Called by PowerBleDelegate when a new crank reading arrives
    function onCrankReading(cumRevs as Lang.Number, eventTime as Lang.Number) as Void {
        if (_prevEvtTime < 0) {
            _prevRevs    = cumRevs;
            _prevEvtTime = eventTime;
            return;
        }
        var deltaRevs = (cumRevs   - _prevRevs)    & 0xFFFF;
        var deltaTime = (eventTime - _prevEvtTime) & 0xFFFF;
        if (deltaRevs > 0 && deltaTime > 0) {
            strokeRate = (deltaRevs * 60 * 1024) / deltaTime;
        }
        if (deltaRevs > 0) { strokeCount += deltaRevs; }
        _prevRevs    = cumRevs;
        _prevEvtTime = eventTime;
    }

    function onPowerReading(watts as Lang.Number) as Void {
        currentPower = watts;

        // Append timestamped reading, then drop anything older than 3 s
        var now = System.getTimer();
        power3sHistory.add([now, watts] as Lang.Array);

        var cutoff = now - 3000;
        var i = 0;
        while (i < power3sHistory.size() &&
               (power3sHistory[i] as Lang.Array)[0] as Lang.Number < cutoff) {
            i++;
        }
        if (i > 0) {
            power3sHistory = power3sHistory.slice(i, null);
        }

        debugHistory.add(watts);
        if (debugHistory.size() > 60) {
            debugHistory = debugHistory.slice(1, null);
        }
    }
}
