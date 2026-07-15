#include "library/aidj/transitionplanner.h"

namespace mixxx {
namespace aidj {

TransitionPlan TransitionPlanner::makePlan(
        bool autoSyncOnTransition,
        bool autoKeylockOnTransition) {
    TransitionPlan plan;
    plan.enableSync = autoSyncOnTransition;
    // Keylock only makes sense when we are also syncing tempo; otherwise
    // leave the deck's existing keylock state alone.
    plan.enableKeylock = autoSyncOnTransition && autoKeylockOnTransition;
    return plan;
}

} // namespace aidj
} // namespace mixxx
