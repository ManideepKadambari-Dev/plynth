#include "railings/RailingSupport.h"

namespace plynth {

namespace {

constexpr double kPost = 0.08;

// Square posts with a top rail and two lower rails.
class PostsRailsStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "posts_rails"; }
    std::string_view name() const override { return "Posts and rails"; }
    int sortKey() const override { return 10; }

    // The posts, the top rail and the two lower rails of one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        return {railing::squarePosts(input, kPost), railing::alongRun(input, Role::Rail, kPost, h - 0.05, h),
                railing::alongRun(input, Role::Rail, 0.05, h * 0.55 - 0.02, h * 0.55 + 0.02),
                railing::alongRun(input, Role::Rail, 0.05, h * 0.2 - 0.02, h * 0.2 + 0.02)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<PostsRailsStyle>());

}

}
