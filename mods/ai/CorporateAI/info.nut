/* GPL-2.0-only; bootstrap fleet caretaker, OpenTTD API 16. */
class CorporateAIInfo extends AIInfo {
    function GetAuthor() { return "Future Logistics contributors"; }
    function GetName() { return "CorporateAI"; }
    function GetDescription() { return "Preserves and runs the engine-built starter network. Expansion is planned."; }
    function GetVersion() { return 3; }
    function GetDate() { return "2026-10-23"; }
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
    }
}
RegisterAI(CorporateAIInfo());
