#include <gtest/gtest.h>

#include "library/aidj/transitionplanner.h"

using mixxx::aidj::TransitionPlan;
using mixxx::aidj::TransitionPlanner;

TEST(TransitionPlannerTest, DisabledByDefault) {
    const TransitionPlan plan = TransitionPlanner::makePlan(false, false);
    EXPECT_TRUE(plan.isNoOp());
    EXPECT_FALSE(plan.enableSync);
    EXPECT_FALSE(plan.enableKeylock);
}

TEST(TransitionPlannerTest, SyncOnly) {
    const TransitionPlan plan = TransitionPlanner::makePlan(true, false);
    EXPECT_FALSE(plan.isNoOp());
    EXPECT_TRUE(plan.enableSync);
    EXPECT_FALSE(plan.enableKeylock);
}

TEST(TransitionPlannerTest, SyncAndKeylock) {
    const TransitionPlan plan = TransitionPlanner::makePlan(true, true);
    EXPECT_TRUE(plan.enableSync);
    EXPECT_TRUE(plan.enableKeylock);
}

TEST(TransitionPlannerTest, KeylockRequiresSync) {
    // Keylock-without-sync is ignored so we do not change pitch alone.
    const TransitionPlan plan = TransitionPlanner::makePlan(false, true);
    EXPECT_TRUE(plan.isNoOp());
    EXPECT_FALSE(plan.enableSync);
    EXPECT_FALSE(plan.enableKeylock);
}
