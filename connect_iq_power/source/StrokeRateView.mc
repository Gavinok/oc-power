import Toybox.Activity;
import Toybox.Application;
import Toybox.FitContributor;
import Toybox.Graphics;
import Toybox.Lang;
import Toybox.WatchUi;

class StrokeRateView extends WatchUi.DataField {

    // Cached values updated in compute()
    private var _strokeRate as Lang.Number  = 0;
    private var _avgSpm     as Lang.Number  = 0;
    private var _bleStatus  as Lang.String  = "Scanning";
    private var _connected  as Lang.Boolean = false;

    // FIT contributor fields (lazy-created on first compute during a recording)
    private var _fitStrokeRate as FitContributor.Field? = null;
    private var _fitAvgSpm     as FitContributor.Field? = null;

    function initialize() {
        DataField.initialize();
    }

    // Called ~1 Hz by the firmware; reads shared state from AppBase.
    function compute(info as Activity.Info) as Void {
        var app = Application.getApp() as StrokeRateApp;

        _strokeRate = app.strokeRate;
        _bleStatus  = app.bleStatus;
        _connected  = app.bleStatus.equals("Connected");

        if (app.spmCount > 0) {
            _avgSpm = (app.totalSpm / app.spmCount).toNumber();
        } else {
            _avgSpm = 0;
        }

        // FIT contribution — lazy-create on first call during a recording
        if (_fitStrokeRate == null) {
            _fitStrokeRate = createField(
                "Stroke Rate", 0, FitContributor.DATA_TYPE_SINT16,
                {:mesgType => FitContributor.MESG_TYPE_RECORD, :units => "spm"}
            );
            _fitAvgSpm = createField(
                "Avg Stroke Rate", 1, FitContributor.DATA_TYPE_SINT16,
                {:mesgType => FitContributor.MESG_TYPE_SESSION, :units => "spm"}
            );
        }

        if (_fitStrokeRate != null) {
            (_fitStrokeRate as FitContributor.Field).setData(_strokeRate);
            (_fitAvgSpm     as FitContributor.Field).setData(_avgSpm);
        }
    }

    // Full repaint
    function onUpdate(dc as Graphics.Dc) as Void {
        var w = dc.getWidth();
        var h = dc.getHeight();

        var bgColor = getBackgroundColor();
        dc.setColor(bgColor, bgColor);
        dc.fillRectangle(0, 0, w, h);

        var fgColor = (bgColor == Graphics.COLOR_BLACK)
            ? Graphics.COLOR_WHITE
            : Graphics.COLOR_BLACK;

        dc.setColor(fgColor, Graphics.COLOR_TRANSPARENT);

        if (!_connected) {
            dc.drawText(
                w / 2, h / 2,
                Graphics.FONT_SMALL,
                _bleStatus + "...",
                Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
            );
            return;
        }

        // Label (top-left)
        dc.drawText(
            4, 4,
            Graphics.FONT_XTINY,
            "SPM",
            Graphics.TEXT_JUSTIFY_LEFT
        );

        // Current stroke rate (large, centred upper half)
        dc.drawText(
            w / 2,
            h / 4,
            Graphics.FONT_NUMBER_THAI_HOT,
            _strokeRate.toString(),
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );

        // Session average (small, lower half)
        dc.drawText(
            w / 2,
            (h * 3) / 4,
            Graphics.FONT_SMALL,
            "Avg: " + _avgSpm.toString(),
            Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER
        );
    }
}
