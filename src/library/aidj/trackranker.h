#pragma once

#include <QList>

#include "library/aidj/mixingcontext.h"
#include "track/trackid.h"

namespace mixxx {
namespace aidj {

/// Pluggable next-track ranking interface.
/// Phase 0/1: RulesTrackRanker. Phase 2: EmbeddingTrackRanker (same contract).
class ITrackRanker {
  public:
    virtual ~ITrackRanker() = default;

    /// Returns candidates ordered best-first. Empty input yields empty output.
    /// Implementations must not mutate context or candidates.
    virtual QList<TrackId> rank(
            const MixingContext& context,
            const QList<RankerCandidate>& candidates) const = 0;
};

} // namespace aidj
} // namespace mixxx
