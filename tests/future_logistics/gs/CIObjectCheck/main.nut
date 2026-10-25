/*
 * GPL-2.0-only. Finds the six Corporate Infrastructure objects by GRF ID and builds each one on open land.
 */
class CIObjectCheck extends GSController {
    function Start() {
        /* The GRF ID "CI\01\01", its bytes in the order written. */
        local grfid = 0x43490101;
        local sizes = [4, 4, 4, 4, 4, 1];
        local built = 0;
        local used = GSTileList();
        for (local id = 0; id < 6; id++) {
            local type = GSObjectType.ResolveNewGRFID(grfid, id);
            if (!GSObjectType.IsValidObjectType(type)) {
                GSLog.Error("CI object id=" + id + " missing");
                continue;
            }
            local name = GSObjectType.GetName(type);
            local size = sizes[id];
            local placed = false;
            for (local y = 8; y < GSMap.GetMapSizeY() - 8 && !placed; y += 3) {
                for (local x = 8; x < GSMap.GetMapSizeX() - 8 && !placed; x += 3) {
                    local tile = GSMap.GetTileIndex(x, y);
                    if (!GSTile.IsBuildableRectangle(tile, size, size) || used.HasItem(tile)) continue;
                    if (GSObjectType.BuildObject(type, 0, tile)) {
                        placed = true;
                        for (local dx = -1; dx <= size; dx++) for (local dy = -1; dy <= size; dy++) used.AddItem(tile + GSMap.GetTileIndex(dx, dy) - GSMap.GetTileIndex(0, 0), 0);
                        GSLog.Info("CI object id=" + id + " name=" + name + " built tile=" + tile);
                        built++;
                    }
                }
            }
            if (!placed) GSLog.Error("CI object id=" + id + " name=" + name + " could not be built: " + GSError.GetLastErrorString());
        }
        GSLog.Info("CI objects built=" + built);
    }
}
