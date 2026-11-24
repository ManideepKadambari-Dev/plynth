#include "dressing/RuleSupport.h"

namespace plynth::dressing {

namespace {

// One to three small props on the top support of tables, desks and cabinets.
class TabletopRule final : public DressingRule {
public:
    std::string_view id() const override { return "tabletop"; }
    std::string_view name() const override { return "Items on tables"; }
    int order() const override { return 20; }

    void place(const DressingInput& in, std::vector<DressingPlacement>& out) const override
    {
        const std::vector<const AssetChoice*> props = assetsWithTag(in.assets, "tabletop");
        if (props.empty()) return;
        for (const Item* item : hostItems(in.floor, in.room)) {
            const AssetChoice* host = findAsset(in.assets, item->asset);
            if (!host || host->supports.empty()) continue;
            if (!holds(host->categories, "table") && !holds(host->categories, "desk") && !holds(host->categories, "cabinet")) {
                continue;
            }
            const AssetSupport* top = &host->supports.front();
            for (const AssetSupport& s : host->supports) {
                if (s.z > top->z) top = &s;
            }
            const int count = 1 + std::min(2, static_cast<int>(seededUnit(in.seed, item->id + "/count") * 3.0));
            const double slot = (top->x1 - top->x0) / count;
            for (int i = 0; i < count; ++i) {
                std::vector<const AssetChoice*> fitting;
                for (const AssetChoice* a : props) {
                    if (a->size.width <= slot + 1e-9 && a->size.depth <= top->y1 - top->y0 + 1e-9
                        && a->size.height <= top->clearance + 1e-9) {
                        fitting.push_back(a);
                    }
                }
                const AssetChoice* prop = pickAsset(in.seed, item->id + "/top/" + std::to_string(i), fitting);
                if (!prop) continue;
                DressingPlacement p;
                p.host = item->id;
                p.rule = "tabletop";
                p.asset = prop->id;
                p.position = localToPlan(*item, top->x0 + (i + 0.5) * slot, (top->y0 + top->y1) / 2.0);
                p.z = item->z + top->z;
                p.rotation = item->rotation;
                p.size = prop->size;
                if (!overlapsAny(p, in.placed) && !overlapsAny(p, out)) out.push_back(std::move(p));
            }
        }
    }
};

const DressingRuleRegistrar registrar(std::make_unique<TabletopRule>());

}

}
