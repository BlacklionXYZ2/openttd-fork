/* GPL-2.0-only. The engine supplies the fleet, orders and capital. */
class CorporateAI extends AIController {
    function Start() {
        AILog.Info("CorporateAI operating the engine-built starter network; expansion is not implemented.");
        AILog.Info("CorporateAI profile " + AIController.GetSetting("profile"));
        while (true) this.Sleep(74);
    }
    function Save() { return {}; }
    function Load(version, data) {}
}
