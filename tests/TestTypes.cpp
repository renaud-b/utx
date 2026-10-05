#include <gtest/gtest.h>

#include "domain/Types.hpp"

using namespace utx::app::domain;

TEST(TargetProjectorCompositionTest, AddsOwnerPolicyAndKindProjectorInOrder) {
    const auto projectors = compose_genesis_projectors(
        TargetKind::Html,
        {"DecentralizedProjector@policy-chain"}
    );

    EXPECT_EQ(
        projectors,
        (std::vector<std::string>{
            "OwnerProjector",
            "DecentralizedProjector@policy-chain",
            "WebProjector"
        })
    );
}

TEST(TargetProjectorCompositionTest, DeduplicatesProjectors) {
    const auto projectors = compose_genesis_projectors(
        TargetKind::Graph,
        {
            "DecentralizedProjector@policy-chain",
            "DecentralizedProjector@policy-chain",
            "GraphProjector"
        }
    );

    EXPECT_EQ(
        projectors,
        (std::vector<std::string>{
            "OwnerProjector",
            "DecentralizedProjector@policy-chain",
            "GraphProjector"
        })
    );
}

TEST(DeployTargetJsonTest, PreservesGenesisProjectors) {
    DeployTarget target;
    target.path = "web/index.html";
    target.chain = "site-chain";
    target.kind = TargetKind::Html;
    target.genesis_projectors = {
        "OwnerProjector",
        "DecentralizedProjector@policy-chain",
        "WebProjector"
    };

    const auto encoded = nlohmann::json(target);
    const auto decoded = encoded.get<DeployTarget>();

    EXPECT_EQ(decoded.genesis_projectors, target.genesis_projectors);
}
