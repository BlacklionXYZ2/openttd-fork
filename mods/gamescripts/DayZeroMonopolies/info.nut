/* GPL-2.0-only; Future Logistics world events, OpenTTD API 16. */
class DayZeroMonopoliesInfo extends GSInfo {
    function GetAuthor() { return "Future Logistics contributors"; }
    function GetName() { return "DayZeroMonopolies"; }
    function GetDescription() { return "Technology milestone news. Funding and company creation belong to the engine."; }
    function GetVersion() { return 1; }
    function GetDate() { return "2026-10-23"; }
    function GetShortName() { return "DZMO"; }
    function GetAPIVersion() { return "16"; }
    function CreateInstance() { return "DayZeroMonopolies"; }
}
RegisterGS(DayZeroMonopoliesInfo());
