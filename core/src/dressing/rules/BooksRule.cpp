#include "dressing/RuleSupport.h"

namespace plynth::dressing {

namespace {

// Rows of books on the supports of shelves, filling 40 to 80 percent of each support's width.
class BooksRule final : public DressingRule {
public:
    std::string_view id() const override { return "books"; }
    std::string_view name() const override { return "Books on shelves"; }
    int order() const override { return 10; }

    void place(const DressingInput& in, std::vector<DressingPlacement>& out) const override
    {
        const std::vector<const AssetChoice*> books = assetsWithTag(in.assets, "book");
        if (books.empty()) return;
        for (const Item* item : hostItems(in.floor, in.room)) {
            const AssetChoice* host = findAsset(in.assets, item->asset);
            if (!host || !holds(host->categories, "shelf")) continue;
            for (std::size_t si = 0; si < host->supports.size(); ++si) {
                const AssetSupport& s = host->supports[si];
                const std::string key = item->id + "/" + std::to_string(si);
                const double width = s.x1 - s.x0;
                const double target = (0.4 + 0.4 * seededUnit(in.seed, key + "/fill")) * width;
                const double most = 0.8 * width;
                double used = 0.0;
                for (int k = 0; used < target; ++k) {
                    std::vector<const AssetChoice*> fitting;
                    for (const AssetChoice* b : books) {
                        if (b->size.width <= most - used + 1e-9 && b->size.depth <= s.y1 - s.y0 + 1e-9
                            && b->size.height <= s.clearance + 1e-9) {
                            fitting.push_back(b);
                        }
                    }
                    const AssetChoice* book = pickAsset(in.seed, key + "/" + std::to_string(k), fitting);
                    if (!book) break;
                    DressingPlacement p;
                    p.host = item->id;
                    p.rule = "books";
                    p.asset = book->id;
                    p.position = localToPlan(*item, s.x0 + used + book->size.width / 2.0, (s.y0 + s.y1) / 2.0);
                    p.z = item->z + s.z;
                    p.rotation = item->rotation;
                    p.size = book->size;
                    used += book->size.width;
                    if (!overlapsAny(p, in.placed) && !overlapsAny(p, out)) out.push_back(std::move(p));
                }
            }
        }
    }
};

const DressingRuleRegistrar registrar(std::make_unique<BooksRule>());

}

}
