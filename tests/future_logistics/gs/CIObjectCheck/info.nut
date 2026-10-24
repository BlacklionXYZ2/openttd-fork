/*
 * GPL-2.0-only. Test Game Script: builds each Corporate Infrastructure object once and logs the result.
 */
class CIObjectCheck extends GSInfo {
    function GetAuthor()      { return "Future Logistics"; }
    function GetName()        { return "CIObjectCheck"; }
    function GetDescription() { return "Test script: places every Corporate Infrastructure object"; }
    function GetVersion()     { return 1; }
    function GetDate()        { return "2026-10-24"; }
    function CreateInstance() { return "CIObjectCheck"; }
    function GetShortName()   { return "CIOC"; }
    function GetAPIVersion()  { return "15"; }
}
RegisterGS(CIObjectCheck());
