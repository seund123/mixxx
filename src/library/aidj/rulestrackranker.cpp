#include "library/aidj/rulestrackranker.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "track/bpm.h"
#include "track/keyutils.h"
#include "util/math.h"

namespace mixxx {
namespace aidj {
namespace {

constexpr double kUnknownBpmDistance = 1000.0;

enum class KeyRank {
    CompatibleKnown = 0,
    Unknown = 1,
    Incompatible = 2,
};

struct ScoredCandidate {
    TrackId trackId;
    KeyRank keyRank = KeyRank::Unknown;
    double bpmDist = kUnknownBpmDistance;
    bool withinBpmWindow = false;
    bool knownBpm = false;
};

bool isValidBpm(double bpm) {
    return mixxx::Bpm::isValidValue(bpm);
}

KeyRank classifyKey(
        mixxx::track::io::key::ChromaticKey reference,
        mixxx::track::io::key::ChromaticKey candidate) {
    if (candidate == mixxx::track::io::key::INVALID ||
            reference == mixxx::track::io::key::INVALID) {
        return KeyRank::Unknown;
    }
    if (RulesTrackRanker::isKeyCompatible(reference, candidate)) {
        return KeyRank::CompatibleKnown;
    }
    return KeyRank::Incompatible;
}

} // namespace

double RulesTrackRanker::bpmDistance(double referenceBpm, double candidateBpm) {
    if (!isValidBpm(referenceBpm) || !isValidBpm(candidateBpm)) {
        return kUnknownBpmDistance;
    }

    const double ratios[] = {1.0, 2.0, 0.5};
    double best = std::numeric_limits<double>::max();
    for (double ratio : ratios) {
        best = math_min(best, fabs(referenceBpm - candidateBpm * ratio));
    }
    return best;
}

bool RulesTrackRanker::isKeyCompatible(
        mixxx::track::io::key::ChromaticKey reference,
        mixxx::track::io::key::ChromaticKey candidate) {
    if (reference == mixxx::track::io::key::INVALID ||
            candidate == mixxx::track::io::key::INVALID) {
        // Unknown key: do not hard-reject in the helper; ranking demotes unknowns.
        return true;
    }
    const QList<mixxx::track::io::key::ChromaticKey> compatible =
            KeyUtils::getCompatibleKeys(reference);
    return compatible.contains(candidate);
}

QList<TrackId> RulesTrackRanker::rank(
        const MixingContext& context,
        const QList<RankerCandidate>& candidates) const {
    std::vector<ScoredCandidate> scored;
    scored.reserve(static_cast<size_t>(candidates.size()));

    for (const RankerCandidate& candidate : candidates) {
        if (!candidate.trackId.isValid()) {
            continue;
        }
        if (candidate.trackId == context.currentTrackId) {
            continue;
        }
        if (context.excludeTrackIds.contains(candidate.trackId)) {
            continue;
        }

        ScoredCandidate entry;
        entry.trackId = candidate.trackId;
        entry.keyRank = classifyKey(context.currentKey, candidate.key);
        entry.knownBpm = isValidBpm(candidate.bpm) && isValidBpm(context.currentBpm);
        entry.bpmDist = bpmDistance(context.currentBpm, candidate.bpm);
        entry.withinBpmWindow = entry.knownBpm && entry.bpmDist <= context.maxBpmDelta;

        if (context.requireCompatibleKey) {
            // Strict mode: only known-compatible keys.
            if (entry.keyRank != KeyRank::CompatibleKnown) {
                continue;
            }
        }

        scored.push_back(entry);
    }

    std::stable_sort(scored.begin(), scored.end(),
            [](const ScoredCandidate& a, const ScoredCandidate& b) {
                // Prefer known-compatible > unknown > incompatible.
                if (a.keyRank != b.keyRank) {
                    return a.keyRank < b.keyRank;
                }
                // Prefer known BPM inside the window.
                if (a.withinBpmWindow != b.withinBpmWindow) {
                    return a.withinBpmWindow && !b.withinBpmWindow;
                }
                // Prefer known BPM over unknown BPM.
                if (a.knownBpm != b.knownBpm) {
                    return a.knownBpm && !b.knownBpm;
                }
                // Closer BPM wins.
                if (a.bpmDist != b.bpmDist) {
                    return a.bpmDist < b.bpmDist;
                }
                return a.trackId < b.trackId;
            });

    QList<TrackId> result;
    result.reserve(static_cast<int>(scored.size()));
    for (const ScoredCandidate& entry : scored) {
        result.append(entry.trackId);
    }
    return result;
}

} // namespace aidj
} // namespace mixxx
