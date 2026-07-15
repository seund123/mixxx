#pragma once

#include "library/aidj/trackranker.h"

namespace mixxx {
namespace aidj {

/// Rule-based ranker: prefers harmonic compatibility and close BPM
/// (including half/double tempo). Designed so an embedding ranker can
/// later implement ITrackRanker without changing AutoDJ wiring.
class RulesTrackRanker : public ITrackRanker {
  public:
    QList<TrackId> rank(
            const MixingContext& context,
            const QList<RankerCandidate>& candidates) const override;

    /// Smallest absolute BPM distance considering 1x / 2x / 0.5x ratios.
    /// Returns a large sentinel when either BPM is invalid.
    static double bpmDistance(double referenceBpm, double candidateBpm);

    /// True when keys are harmonically compatible (Circle of Fifths).
    /// Unknown keys return true here; ranking demotes unknowns separately,
    /// and requireCompatibleKey excludes them.
    static bool isKeyCompatible(
            mixxx::track::io::key::ChromaticKey reference,
            mixxx::track::io::key::ChromaticKey candidate);
};

} // namespace aidj
} // namespace mixxx
