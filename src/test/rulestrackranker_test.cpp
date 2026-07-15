#include <gtest/gtest.h>

#include <QVariant>

#include "library/aidj/rulestrackranker.h"
#include "proto/keys.pb.h"

namespace {

using mixxx::aidj::MixingContext;
using mixxx::aidj::RankerCandidate;
using mixxx::aidj::RulesTrackRanker;
using mixxx::track::io::key::A_MINOR;
using mixxx::track::io::key::C_MAJOR;
using mixxx::track::io::key::D_MAJOR;
using mixxx::track::io::key::INVALID;

TrackId id(int value) {
    return TrackId(QVariant(value));
}

RankerCandidate candidate(
        int trackId,
        double bpm,
        mixxx::track::io::key::ChromaticKey key) {
    RankerCandidate c;
    c.trackId = id(trackId);
    c.bpm = bpm;
    c.key = key;
    return c;
}

} // namespace

TEST(RulesTrackRankerTest, BpmDistanceHalfDouble) {
    EXPECT_DOUBLE_EQ(0.0, RulesTrackRanker::bpmDistance(128.0, 128.0));
    EXPECT_DOUBLE_EQ(0.0, RulesTrackRanker::bpmDistance(128.0, 64.0));
    EXPECT_DOUBLE_EQ(0.0, RulesTrackRanker::bpmDistance(128.0, 256.0));
    EXPECT_NEAR(2.0, RulesTrackRanker::bpmDistance(128.0, 130.0), 1e-9);
    EXPECT_GT(RulesTrackRanker::bpmDistance(0.0, 128.0), 100.0);
}

TEST(RulesTrackRankerTest, KeyCompatibility) {
    EXPECT_TRUE(RulesTrackRanker::isKeyCompatible(C_MAJOR, C_MAJOR));
    // Relative minor of C major is A minor (Circle of Fifths).
    EXPECT_TRUE(RulesTrackRanker::isKeyCompatible(C_MAJOR, A_MINOR));
    EXPECT_FALSE(RulesTrackRanker::isKeyCompatible(C_MAJOR, D_MAJOR));
    // Unknown keys are treated as compatible/neutral.
    EXPECT_TRUE(RulesTrackRanker::isKeyCompatible(INVALID, C_MAJOR));
    EXPECT_TRUE(RulesTrackRanker::isKeyCompatible(C_MAJOR, INVALID));
}

TEST(RulesTrackRankerTest, RanksByKeyThenBpm) {
    MixingContext context;
    context.currentTrackId = id(1);
    context.currentBpm = 128.0;
    context.currentKey = C_MAJOR;
    context.maxBpmDelta = 6.0;

    QList<RankerCandidate> candidates;
    // Far BPM, incompatible key
    candidates.append(candidate(10, 100.0, D_MAJOR));
    // Close BPM, incompatible key
    candidates.append(candidate(11, 129.0, D_MAJOR));
    // Close BPM, compatible key (same key)
    candidates.append(candidate(12, 130.0, C_MAJOR));
    // Exact BPM, compatible key
    candidates.append(candidate(13, 128.0, C_MAJOR));
    // Current track should be excluded
    candidates.append(candidate(1, 128.0, C_MAJOR));

    const RulesTrackRanker ranker;
    const QList<TrackId> ranked = ranker.rank(context, candidates);

    ASSERT_EQ(4, ranked.size());
    EXPECT_EQ(id(13), ranked.at(0));
    EXPECT_EQ(id(12), ranked.at(1));
    // Incompatible keys come after compatible ones
    EXPECT_EQ(id(11), ranked.at(2));
    EXPECT_EQ(id(10), ranked.at(3));
}

TEST(RulesTrackRankerTest, RequireCompatibleKeyFilters) {
    MixingContext context;
    context.currentBpm = 120.0;
    context.currentKey = C_MAJOR;
    context.requireCompatibleKey = true;

    QList<RankerCandidate> candidates;
    candidates.append(candidate(20, 120.0, D_MAJOR));
    candidates.append(candidate(21, 122.0, C_MAJOR));
    candidates.append(candidate(22, 120.0, INVALID)); // unknown excluded in strict mode
    candidates.append(candidate(23, 120.0, A_MINOR)); // compatible

    const RulesTrackRanker ranker;
    const QList<TrackId> ranked = ranker.rank(context, candidates);

    ASSERT_EQ(2, ranked.size());
    EXPECT_EQ(id(23), ranked.at(0));
    EXPECT_EQ(id(21), ranked.at(1));
}

TEST(RulesTrackRankerTest, PrefersWithinBpmWindow) {
    MixingContext context;
    context.currentBpm = 128.0;
    context.currentKey = C_MAJOR;
    context.maxBpmDelta = 4.0;

    QList<RankerCandidate> candidates;
    candidates.append(candidate(30, 140.0, C_MAJOR)); // outside window
    candidates.append(candidate(31, 130.0, C_MAJOR)); // inside window

    const RulesTrackRanker ranker;
    const QList<TrackId> ranked = ranker.rank(context, candidates);

    ASSERT_EQ(2, ranked.size());
    EXPECT_EQ(id(31), ranked.at(0));
    EXPECT_EQ(id(30), ranked.at(1));
}

TEST(RulesTrackRankerTest, DemotesUnknownBpmAndKey) {
    MixingContext context;
    context.currentBpm = 128.0;
    context.currentKey = C_MAJOR;
    context.maxBpmDelta = 6.0;

    QList<RankerCandidate> candidates;
    candidates.append(candidate(40, 0.0, INVALID));   // unknown bpm+key
    candidates.append(candidate(41, 128.0, C_MAJOR)); // known match

    const RulesTrackRanker ranker;
    const QList<TrackId> ranked = ranker.rank(context, candidates);

    ASSERT_EQ(2, ranked.size());
    EXPECT_EQ(id(41), ranked.at(0));
    EXPECT_EQ(id(40), ranked.at(1));
}
