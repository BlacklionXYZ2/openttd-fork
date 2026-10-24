/*
 * GPL-2.0-only. CorporateAI runs a Future Logistics rival after day zero.
 *
 * The engine builds the day-zero network. Afterwards this AI keeps the company going with the same
 * choices: AIFutureLogistics gives it the rival profile's recipes and adoption rules, the day-zero
 * planners and the Tech Library, so technology added by NewGRFs reaches it with no change here.
 */
class CorporateAI extends AIController {
    profile = 0;
    aggression = 1;
    months = 0;      ///< Months run, for pacing.
    built = null;    ///< Services this AI built per recipe key, so new recipes come first.
    replaced = null; ///< The engine each replaced engine is set to become.
    selling = null;  ///< Vehicles on their way to a depot to be sold.

    constructor() {
        this.built = {};
        this.replaced = {};
        this.selling = {};
    }

    function Start() {
        this.profile = AIController.GetSetting("profile");
        this.aggression = AIController.GetSetting("aggression");
        AILog.Info("CorporateAI profile " + this.profile + " aggression " + this.aggression);
        if (!AIFutureLogistics.IsAvailable()) AILog.Warning("CorporateAI: no new routes in multiplayer games; fleet care only");
        /* The game renews worn-out vehicles itself once they visit a depot. */
        AICompany.SetAutoRenewStatus(true);
        local month = AIDate.GetMonth(AIDate.GetCurrentDate());
        while (true) {
            this.HandleEvents();
            local now = AIDate.GetMonth(AIDate.GetCurrentDate());
            if (now != month) {
                month = now;
                this.Monthly();
            }
            this.Sleep(74);
        }
    }

    /** Report crashes; AddVehicles refills a route that lost a vehicle once cargo waits again. */
    function HandleEvents() {
        while (AIEventController.IsEventWaiting()) {
            local event = AIEventController.GetNextEvent();
            if (event.GetEventType() != AIEvent.ET_VEHICLE_CRASHED) continue;
            local crash = AIEventVehicleCrashed.Convert(event);
            AILog.Warning("Vehicle " + crash.GetVehicleID() + " crashed at tile " + crash.GetCrashSite() + " (reason " + crash.GetCrashReason() + ")");
        }
    }

    function Monthly() {
        this.months++;
        this.ManageLoan();
        if (this.months % 12 == 1) this.SetReplacements();
        this.SellInDepot();
        if (this.months % 12 == 6) this.SellLosers();
        this.SendForService();
        this.AddVehicles();
        local interval = [24, 12, 6][this.aggression];
        if (this.months % interval == 0) this.Expand();
    }

    /** Repay what is not needed, keeping a reserve; borrow if the balance goes below zero. */
    function ManageLoan() {
        local reserve = 100000;
        local balance = AICompany.GetBankBalance(AICompany.COMPANY_SELF);
        local loan = AICompany.GetLoanAmount();
        if (balance < 0) {
            AICompany.SetMinimumLoanAmount(min(AICompany.GetMaxLoanAmount(), loan - balance + reserve));
        } else if (balance > reserve && loan > 0) {
            AICompany.SetMinimumLoanAmount(max(0, loan - (balance - reserve)));
        }
    }

    /** Ask the Tech Library what each vehicle type should become, and let autoreplace do it. */
    function SetReplacements() {
        foreach (vehicle, _ in AIVehicleList()) {
            if (!AIVehicle.IsPrimaryVehicle(vehicle)) continue;
            local engine = AIVehicle.GetEngineType(vehicle);
            local better = AIFutureLogistics.GetReplacement(vehicle);
            if (!AIEngine.IsValidEngine(better) || better == engine || !AIEngine.IsBuildable(better)) continue;
            /* A newer choice replaces an older rule, so vehicles go straight to the best one. */
            if ((engine in this.replaced) && this.replaced[engine] == better) continue;
            if (AIGroup.SetAutoReplace(AIGroup.GROUP_ALL, engine, better)) {
                this.replaced[engine] <- better;
                AILog.Info("Replacing " + AIEngine.GetName(engine) + " with " + AIEngine.GetName(better));
            }
        }
    }

    /** Vehicles only get renewed or replaced in a depot; with breakdowns off they never go by themselves. */
    function SendForService() {
        foreach (vehicle, _ in AIVehicleList()) {
            if (!AIVehicle.IsPrimaryVehicle(vehicle) || AIVehicle.IsInDepot(vehicle) || (vehicle in this.selling)) continue;
            local engine = AIVehicle.GetEngineType(vehicle);
            if ((engine in this.replaced) || AIVehicle.GetAgeLeft(vehicle) < 365) AIVehicle.SendVehicleToDepotForServicing(vehicle);
        }
    }

    /** Send a vehicle that lost money last year and this year to a depot, if its route has others to carry on. */
    function SellLosers() {
        foreach (key, route in this.Routes()) {
            if (route.count < 2) continue;
            foreach (vehicle, _ in AIVehicleList()) {
                if (!AIVehicle.IsPrimaryVehicle(vehicle) || (vehicle in this.selling) || AIVehicle.GetAge(vehicle) < 730) continue;
                if (AIOrder.GetOrderCount(vehicle) < 2 || this.RouteKey(vehicle, route.cargo) != key) continue;
                if (AIVehicle.GetProfitLastYear(vehicle) >= 0 || AIVehicle.GetProfitThisYear(vehicle) >= 0) continue;
                if (AIVehicle.SendVehicleToDepot(vehicle)) {
                    this.selling[vehicle] <- true;
                    AILog.Info("Selling a vehicle that keeps losing money: " + AIVehicle.GetName(vehicle));
                }
                break;
            }
        }
    }

    /**
     * Sell vehicles sent to a depot for selling once they arrive. Autoreplace can renew a vehicle in the
     * depot under a new ID, so any vehicle of ours stopped in a depot is sold: this AI never stops one otherwise.
     */
    function SellInDepot() {
        foreach (vehicle, _ in AIVehicleList()) {
            if (AIVehicle.IsPrimaryVehicle(vehicle) && AIVehicle.IsStoppedInDepot(vehicle)) AIVehicle.SellVehicle(vehicle);
        }
        local done = [];
        foreach (vehicle, _ in this.selling) {
            if (!AIVehicle.IsValidVehicle(vehicle)) done.append(vehicle);
        }
        foreach (vehicle in done) delete this.selling[vehicle];
    }

    /** The station a vehicle loads at: its full-load order, or its first order. */
    function LoadingStation(vehicle) {
        local order = 0;
        for (local i = 0; i < AIOrder.GetOrderCount(vehicle); i++) {
            if ((AIOrder.GetOrderFlags(vehicle, i) & AIOrder.OF_FULL_LOAD_ANY) != 0) {
                order = i;
                break;
            }
        }
        return AIStation.GetStationID(AIOrder.GetOrderDestination(vehicle, order));
    }

    /** The cargo a vehicle carries. */
    function CargoOf(vehicle) {
        foreach (cargo, _ in AICargoList()) {
            if (AIVehicle.GetCapacity(vehicle, cargo) > 0) return cargo;
        }
        return -1;
    }

    /** A route's identity: its cargo and the stations it calls at. Day-zero vehicles do not share orders, so orders cannot tell. */
    function RouteKey(vehicle, cargo) {
        local stations = [];
        for (local i = 0; i < AIOrder.GetOrderCount(vehicle); i++) {
            stations.append(AIStation.GetStationID(AIOrder.GetOrderDestination(vehicle, i)));
        }
        stations.sort();
        local key = "" + cargo;
        foreach (station in stations) key += "/" + station;
        return key;
    }

    /** The company's routes: cargo and stations, with one vehicle on each and how many run it. */
    function Routes() {
        local routes = {};
        foreach (vehicle, _ in AIVehicleList()) {
            if (!AIVehicle.IsPrimaryVehicle(vehicle) || AIOrder.GetOrderCount(vehicle) < 2) continue;
            local cargo = this.CargoOf(vehicle);
            if (cargo < 0) continue;
            local key = this.RouteKey(vehicle, cargo);
            if (key in routes) {
                routes[key].count++;
            } else {
                routes[key] <- {vehicle = vehicle, cargo = cargo, count = 1};
            }
        }
        return routes;
    }

    /** Add a vehicle to each route where more than two loads are waiting, up to a size set by aggression. */
    function AddVehicles() {
        local largest = [3, 5, 8][this.aggression];
        foreach (key, route in this.Routes()) {
            local vehicle = route.vehicle;
            if (route.count >= largest) continue;
            /* Only grow routes that pay, once they have had time to. */
            if (AIVehicle.GetAge(vehicle) > 365 && AIVehicle.GetProfitLastYear(vehicle) <= 0) continue;
            local station = this.LoadingStation(vehicle);
            if (!AIStation.IsValidStation(station)) continue;
            local capacity = AIVehicle.GetCapacity(vehicle, route.cargo);
            if (AIStation.GetCargoWaiting(station, route.cargo) < capacity * 2) continue;
            /* A depot on the route's own track or road, so the copy can reach it. */
            local depot = AIFutureLogistics.GetRouteDepot(vehicle);
            if (!AIMap.IsValidTile(depot)) continue;
            local copy = AIVehicle.CloneVehicle(depot, vehicle, true);
            if (!AIVehicle.IsValidVehicle(copy)) continue;
            AIVehicle.StartStopVehicle(copy);
            AILog.Info("Added a vehicle to " + AIStation.GetName(station) + " for " + AICargo.GetCargoLabel(route.cargo) + " (" + (route.count + 1) + " on the route)");
        }
    }

    /** Open one new route: the recipe this AI has built least, in the profile's order. */
    function Expand() {
        if (this.profile == 0 || !AIFutureLogistics.IsAvailable()) return;
        local count = AIFutureLogistics.GetPlanSize(this.profile);
        local order = [];
        for (local i = 0; i < count; i++) {
            local recipe = AIFutureLogistics.GetPlanRecipe(this.profile, i);
            order.append({recipe = recipe, times = (recipe in this.built) ? this.built[recipe] : 0, index = i});
        }
        order.sort(function(a, b) {
            if (a.times != b.times) return a.times - b.times;
            return a.index - b.index;
        });
        /* Building needs the cash up front; ManageLoan repays what is left over. */
        AICompany.SetLoanAmount(AICompany.GetMaxLoanAmount());
        foreach (item in order) {
            if (AIFutureLogistics.BuildService(this.profile, item.recipe)) {
                this.built[item.recipe] <- item.times + 1;
                AILog.Info("Opened a new " + item.recipe + " route");
                return;
            }
        }
        AILog.Info("No new route could be opened this time");
    }

    function Save() {
        return {months = this.months, built = this.built};
    }

    function Load(version, data) {
        if ("months" in data) this.months = data.months;
        if ("built" in data) this.built = data.built;
    }
}
