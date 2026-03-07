import Toybox.Application;
import Toybox.Lang;
import Toybox.WatchUi;

class DebugPowerApp extends PaddlePowerApp {
    function initialize() { PaddlePowerApp.initialize(); }
    function getInitialView() as [WatchUi.Views] or [WatchUi.Views, WatchUi.InputDelegates] {
        return [new DebugPowerView()];
    }
}
