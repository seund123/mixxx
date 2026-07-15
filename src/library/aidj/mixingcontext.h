#pragma once

#include <QSet>

#include "proto/keys.pb.h"
#include "track/trackid.h"

namespace mixxx {
namespace aidj {

/// Snapshot of the current mix state used to rank next-track candidates.
/// Intentionally free of TrackPointer so rankers stay testable and ML-ready.
struct MixingContext {
    TrackId currentTrackId;
    double currentBpm = 0.0;
    mixxx::track::io::key::ChromaticKey currentKey =
            mixxx::track::io::key::INVALID;

    /// Soft BPM window in BPM units (half/double tempo allowed).
    double maxBpmDelta = 6.0;

    /// When true, incompatible keys are ranked after compatible ones
    /// (and may be filtered by the ranker).
    bool requireCompatibleKey = false;

    QSet<TrackId> excludeTrackIds;
};

/// Lightweight candidate metadata for ranking without loading full Track objects
/// into the scorer itself.
struct RankerCandidate {
    TrackId trackId;
    double bpm = 0.0;
    mixxx::track::io::key::ChromaticKey key = mixxx::track::io::key::INVALID;
};

} // namespace aidj
} // namespace mixxx
