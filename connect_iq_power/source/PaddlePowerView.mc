import Toybox.Activity;
import Toybox.Application;
import Toybox.Graphics;
import Toybox.Lang;
import Toybox.WatchUi;

class PaddlePowerView extends WatchUi.DataField {

    // Cached metric values updated in compute()
    private var _currentPower as Lang.Number = 0;
    private var _avg3sPower   as Lang.Number = 0;
    private var _sessionAvg   as Lang.Number = 0;
    private var _bleStatus    as Lang.String = "Scanning";
    private var _connected    as Lang.Boolean = false;

    function initialize() {
        DataField.initialize();
    }

    // Called ~1 Hz by the firmware; read shared state from AppBase.
    function compute(info as Activity.Info) as Void {
        var app = Application.getApp() as PaddlePowerApp;

        _currentPower = app.currentPower;
        _bleStatus    = app.bleStatus;
        _connected    = (app.bleStatus.equals("Connected"));

        // True 3-second rolling average from time-stamped history
        var history = app.power3sHistory;
        var sum     = 0;
        var count   = history.size();
        for (var i = 0; i < count; i++) {
            sum += (history[i] as Lang.Array)[1] as Lang.Number;
        }
        _avg3sPower = (count > 0) ? (sum / count) : 0;

        // Session average
        if (app.readingCount > 0) {
            _sessionAvg = (app.totalPower / app.readingCount).toNumber();
        } else {
            _sessionAvg = 0;
        }
    }

    // Full repaint
    function onUpdate(dc as Graphics.Dc) as Void {
        var w = dc.getWidth();
        var h = dc.getHeight();

        // Background
        var bgColor = getBackgroundColor();
        dc.setColor(bgColor, bgColor);
        dc.fillRectangle(0, 0, w, h);

        // Choose foreground based on background luminance
        var fgColor = (bgColor == Graphics.COLOR_BLACK)
            ? Graphics.COLOR_WHITE
            : Graphics.COLOR_BLACK;

        if (!_connected) {
            _drawStatusScreen(dc, w, h, fgColor);
            return;
        }

        _drawMetrics(dc, w, h, fgColor);
    }

    // Draw the three-metric layout
    private function _drawMetrics(dc  as Graphics.Dc,
                                   w   as Lang.Number,
                                   h   as Lang.Number,
                                   fg  as Lang.Number) as Void {
        // --- Row 1: label "PWR" (small, top-left) ---
        dc.setColor(fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(
            4, 4,
            Graphics.FONT_XTINY,
            "PWR",
            Graphics.TEXT_JUSTIFY_LEFT
        );

        // --- Row 2: current power (large, centred) ---
        var powerStr = _currentPower.toString() + " W";
        dc.drawText(
            w / 2,
            h / 4,
            Graphics.FONT_NUMBER_THAI_HOT,
            powerStr,
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );

        // --- Row 3: 3s avg and session avg (small, two columns) ---
        var bottomY = (h * 3) / 4;
        var leftX   = w / 4;
        var rightX  = (w * 3) / 4;

        dc.drawText(
            leftX, bottomY,
            Graphics.FONT_SMALL,
            "3s: " + _avg3sPower.toString(),
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );
        dc.drawText(
            rightX, bottomY,
            Graphics.FONT_SMALL,
            "Avg:" + _sessionAvg.toString(),
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );
    }

    // Show status when BLE is not yet receiving data
    private function _drawStatusScreen(dc  as Graphics.Dc,
                                        w   as Lang.Number,
                                        h   as Lang.Number,
                                        fg  as Lang.Number) as Void {
        dc.setColor(fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(
            w / 2, h / 2,
            Graphics.FONT_SMALL,
            _bleStatus + "...",
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );
    }
}
