/* GPL-2.0-only. No capital grants, company creation or vehicle unlocks. */
class DayZeroMonopolies extends GSController {
    announced = null;
    constructor() { this.announced = {}; }
    function Save() { return { announced = this.announced }; }
    function Load(version, data) {
        if ("announced" in data) this.announced = data.announced;
    }
    function Start() {
        GSLog.Info("Future Logistics world events started; the engine owns day-zero funding.");
        // Map-generation GS execution precedes human/company bootstrap. Sleep first.
        this.Sleep(74);
        local companies = 0;
        for (local i = 0; i < 15; i++) if (GSCompany.ResolveCompanyID(i) != GSCompany.COMPANY_INVALID) companies++;
        GSLog.Info("Companies visible after startup: " + companies);
        local milestones = [
            [2022, "Future Logistics roadmap: parcel drones. Vehicle content is planned."],
            [2035, "Future Logistics roadmap: hyperloop freight. Vehicle content is planned."],
            [2050, "Future Logistics roadmap: rocket freight. Vehicle content is planned."]
        ];
        while (true) {
            local year = GSDate.GetYear(GSDate.GetCurrentDate());
            foreach (milestone in milestones) {
                local key = milestone[0].tostring();
                // Late starts report each reached milestone once; save data prevents repeats.
                if (year >= milestone[0] && !(key in this.announced)) {
                    if (GSNews.Create(GSNews.NT_GENERAL, milestone[1], GSCompany.COMPANY_INVALID, GSNews.NR_NONE, 0)) {
                        this.announced[key] <- true;
                        GSLog.Info("Published roadmap milestone " + key);
                    }
                }
            }
            this.Sleep(74);
        }
    }
}
