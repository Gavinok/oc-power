import Toybox.Application;
import Toybox.BluetoothLowEnergy;
import Toybox.Lang;
import Toybox.WatchUi;

class StrokeRateApp extends Application.AppBase {

    // Shared stroke rate metrics (read by StrokeRateView.compute())
    var strokeRate as Lang.Number = 0;   // current strokes/min
    var totalSpm   as Lang.Float  = 0.0f;
    var spmCount   as Lang.Number = 0;
    var bleStatus  as Lang.String = "Scanning";

    private var _prevRevs    as Lang.Number = -1;  // -1 = no previous reading
    private var _prevEvtTime as Lang.Number = 0;

    private var _bleDelegate as PowerBleDelegate?;

    function initialize() {
        AppBase.initialize();
    }

    function onStart(state as Lang.Dictionary?) as Void {
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
        return [new StrokeRateView()];
    }

    // Unused by this field — power value is ignored
    function onPowerReading(watts as Lang.Number) as Void {
    }

    // Called by PowerBleDelegate on every BLE packet (crank data always present)
    function onCrankReading(cumRevs as Lang.Number, eventTime as Lang.Number) as Void {
        if (_prevRevs < 0) {
            _prevRevs    = cumRevs;
            _prevEvtTime = eventTime;
            return;
        }

        // uint16 wraparound-safe deltas
        var deltaRevs = (cumRevs   - _prevRevs)    & 0xFFFF;
        var deltaTime = (eventTime - _prevEvtTime) & 0xFFFF;  // units: 1/1024 s

        if (deltaRevs > 0 && deltaTime > 0) {
            var spm = (deltaRevs * 60 * 1024) / deltaTime;
            strokeRate = spm;
            totalSpm  += spm.toFloat();
            spmCount  += 1;
        }

        _prevRevs    = cumRevs;
        _prevEvtTime = eventTime;
    }
}
