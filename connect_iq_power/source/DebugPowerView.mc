import Toybox.Activity;
import Toybox.Application;
import Toybox.Graphics;
import Toybox.Lang;
import Toybox.WatchUi;

class DebugPowerView extends WatchUi.DataField {

    private var _currentPower as Lang.Number = 0;
    private var _avg3sPower   as Lang.Number = 0;
    private var _strokeRate   as Lang.Number = 0;
    private var _strokeCount  as Lang.Number = 0;
    private var _connected    as Lang.Boolean = false;
    private var _debugHistory as Lang.Array = [] as Lang.Array;

    function initialize() {
        DataField.initialize();
    }

    function compute(info as Activity.Info) as Void {
        var app = Application.getApp() as PaddlePowerApp;

        _currentPower = app.currentPower;
        _connected    = app.bleStatus.equals("Connected");
        _strokeRate   = app.strokeRate;
        _strokeCount  = app.strokeCount;
        _debugHistory = app.debugHistory;

        var history = app.power3sHistory;
        var sum     = 0;
        var count   = history.size();
        for (var i = 0; i < count; i++) {
            sum += (history[i] as Lang.Array)[1] as Lang.Number;
        }
        _avg3sPower = (count > 0) ? (sum / count) : 0;
    }

    function onUpdate(dc as Graphics.Dc) as Void {
        var w = dc.getWidth();
        var h = dc.getHeight();

        var bgColor = getBackgroundColor();
        dc.setColor(bgColor, bgColor);
        dc.fillRectangle(0, 0, w, h);

        var fgColor = (bgColor == Graphics.COLOR_BLACK)
            ? Graphics.COLOR_WHITE
            : Graphics.COLOR_BLACK;
        var barColor = (bgColor == Graphics.COLOR_BLACK)
            ? Graphics.COLOR_DK_GREEN
            : Graphics.COLOR_BLUE;

        // Layout constants
        var topH    = 28;
        var bottomH = 30;
        var chartTop = topH;
        var chartH  = h - topH - bottomH;

        // --- Top row: SPM left, stroke count + BLE dot right ---
        var midY = topH / 2;
        dc.setColor(fgColor, Graphics.COLOR_TRANSPARENT);
        dc.drawText(2, midY, Graphics.FONT_TINY,
            "SPM:" + _strokeRate.toString(),
            Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);

        var dotColor = _connected ? Graphics.COLOR_GREEN : Graphics.COLOR_RED;
        dc.setColor(dotColor, Graphics.COLOR_TRANSPARENT);
        dc.drawText(w - 2, midY, Graphics.FONT_TINY, "●",
            Graphics.TEXT_JUSTIFY_RIGHT | Graphics.TEXT_JUSTIFY_VCENTER);

        dc.setColor(fgColor, Graphics.COLOR_TRANSPARENT);
        dc.drawText(w / 2, midY, Graphics.FONT_TINY,
            "#" + _strokeCount.toString(),
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);

        // --- Bar chart ---
        var numBars = _debugHistory.size();
        if (numBars > 0) {
            var barW = w / 60;
            if (barW < 1) { barW = 1; }

            // Find max power, minimum 100 to avoid div-by-zero on low data
            var maxPwr = 100;
            for (var i = 0; i < numBars; i++) {
                var v = _debugHistory[i] as Lang.Number;
                if (v > maxPwr) { maxPwr = v; }
            }

            dc.setColor(barColor, Graphics.COLOR_TRANSPARENT);
            // Draw bars left-aligned; newest bars are at the right
            var startX = (60 - numBars) * barW;
            for (var i = 0; i < numBars; i++) {
                var v    = _debugHistory[i] as Lang.Number;
                var barH = (v * chartH) / maxPwr;
                if (barH < 1) { barH = 1; }
                dc.fillRectangle(startX + i * barW, chartTop + chartH - barH, barW - 1, barH);
            }
        }

        // --- Bottom row: 3s avg and current power ---
        var bottomY = h - bottomH / 2;
        dc.setColor(fgColor, Graphics.COLOR_TRANSPARENT);
        dc.drawText(2, bottomY, Graphics.FONT_SMALL,
            "3s:" + _avg3sPower.toString() + "W",
            Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
        dc.drawText(w - 2, bottomY, Graphics.FONT_SMALL,
            "NOW:" + _currentPower.toString() + "W",
            Graphics.TEXT_JUSTIFY_RIGHT | Graphics.TEXT_JUSTIFY_VCENTER);
    }
}
