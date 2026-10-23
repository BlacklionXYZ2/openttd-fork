/* GPL-2.0-only; bootstrap fleet caretaker, OpenTTD API 16. */
class CorporateAIInfo extends AIInfo {
    function GetAuthor() { return "Future Logistics contributors"; }
    function GetName() { return "CorporateAI"; }
    function GetDescription() { return "Preserves and runs the engine-built bus fleet. Expansion is planned."; }
    function GetVersion() { return 1; }
    function GetDate() { return "2026-10-23"; }
    function GetShortName() { return "FLCA"; }
    function GetAPIVersion() { return "16"; }
    function CreateInstance() { return "CorporateAI"; }
}
RegisterAI(CorporateAIInfo());
