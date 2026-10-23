/* GPL-2.0-only; Future Logistics rival, OpenTTD API 16 plus this fork's AIFutureLogistics. */
class CorporateAIInfo extends AIInfo {
    function GetAuthor() { return "Future Logistics contributors"; }
    function GetName() { return "CorporateAI"; }
    function GetDescription() { return "Runs a Future Logistics rival after day zero: renews and upgrades its fleet, adds vehicles where cargo waits, and opens new routes with the same recipes and NewGRF tech as day zero. Needs the Future Logistics build of OpenTTD."; }
    function GetVersion() { return 4; }
    function GetDate() { return "2026-10-24"; }
    function GetShortName() { return "FLCA"; }
    function GetAPIVersion() { return "16"; }
    function CreateInstance() { return "CorporateAI"; }
    function GetSettings() {
        /* Set by the engine at day zero; saved with the game. 0 means the company has no profile. */
        /* Profiles above 4 are rivals declared by NewGRFs; see docs/future_logistics/tech_format.md. */
        AddSetting({name = "profile", description = "Day-zero company profile", min_value = 0, max_value = 15,
            default_value = 0, flags = CONFIG_NONE});
        AddLabels("profile", {_0 = "None", _1 = "National Rail & Freight", _2 = "AeroLink",
            _3 = "Metro & Regional", _4 = "Integrated Logistics"});
        AddSetting({name = "aggression", description = "How fast the rival expands after day zero", min_value = 0, max_value = 2,
            default_value = 1, flags = CONFIG_INGAME});
        AddLabels("aggression", {_0 = "Low: a new route every two years", _1 = "Medium: a new route every year",
            _2 = "High: a new route every six months"});
    }
}
RegisterAI(CorporateAIInfo());
