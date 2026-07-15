#pragma once

namespace mixxx {
namespace aidj {

/// Intent for how Auto DJ should prepare decks for a beatmatched transition.
/// Kept free of ControlObject / engine types so it stays testable and ML-ready.
struct TransitionPlan {
    bool enableSync = false;
    bool enableKeylock = false;

    bool isNoOp() const {
        return !enableSync && !enableKeylock;
    }
};

/// Builds a TransitionPlan from Auto DJ preferences.
class TransitionPlanner {
  public:
    static TransitionPlan makePlan(bool autoSyncOnTransition, bool autoKeylockOnTransition);
};

} // namespace aidj
} // namespace mixxx
