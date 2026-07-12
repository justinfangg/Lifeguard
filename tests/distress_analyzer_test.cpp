#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "lifeguard/distress_analyzer.hpp"

int main() {
    using namespace lifeguard;
    constexpr uint64_t second = 1000000000ULL;

    Track track;
    track.id = 7;
    track.box = cv::Rect2f(100.0f, 100.0f, 100.0f, 200.0f);
    track.centroids = {
        {0, {150.0f, 160.0f}},
        {second / 3, {150.0f, 205.0f}},
        {2 * second / 3, {150.0f, 150.0f}},
        {second, {150.0f, 210.0f}}};

    Pose no_pose;
    DistressAnalyzer analyzer(
        {0.4f, 1.0f, 2.0f, 0.4f, 1.0f, 2.0f, 3.0f});
    const auto first = analyzer.evaluate(track, no_pose, second);
    assert(!first.potential);
    assert(first.horizontally_stationary);
    assert(first.score >= 0.35f);
    assert(first.reason.find("bobbing") != std::string::npos);
    assert(!first.alerting);

    const auto held = analyzer.evaluate(track, no_pose, 3 * second);
    assert(held.potential);
    assert(held.alerting);

    track.centroids = {
        {3 * second, {150.0f, 180.0f}},
        {4 * second, {150.0f, 180.0f}}};
    const auto recovering = analyzer.evaluate(track, no_pose, 4 * second);
    assert(recovering.potential);
    assert(recovering.alerting);
    for (uint64_t t = 5; t <= 8; ++t) {
        analyzer.evaluate(track, no_pose, t * second);
    }
    const auto cleared = analyzer.evaluate(track, no_pose, 11 * second);
    assert(!cleared.potential);
    assert(!cleared.alerting);

    // Bobbing alone can exceed the score thresholds, but a swimmer making
    // horizontal progress must never be classified as drowning.
    Track moving = track;
    moving.id = 8;
    moving.centroids = {
        {0, {150.0f, 160.0f}},
        {second / 3, {175.0f, 205.0f}},
        {2 * second / 3, {205.0f, 150.0f}},
        {second, {230.0f, 210.0f}}};
    DistressAnalyzer moving_analyzer(
        {0.3f, 1.0f, 2.0f, 0.3f, 1.0f, 2.0f, 3.0f});
    const auto moving_first =
        moving_analyzer.evaluate(moving, no_pose, second);
    const auto moving_held =
        moving_analyzer.evaluate(moving, no_pose, 3 * second);
    assert(moving_first.score >= 0.3f);
    assert(!moving_first.horizontally_stationary);
    assert(!moving_first.potential);
    assert(!moving_held.potential);
    assert(!moving_held.alerting);

    moving.id = 9;
    moving.centroids = {
        {0, {150.0f, 160.0f}},
        {second / 3, {230.0f, 205.0f}},
        {2 * second / 3, {190.0f, 150.0f}},
        {second, {150.0f, 210.0f}}};
    const auto out_and_back =
        moving_analyzer.evaluate(moving, no_pose, second);
    assert(out_and_back.score >= 0.3f);
    assert(!out_and_back.horizontally_stationary);
    assert(!out_and_back.potential);

    std::cout << "distress analyzer stationarity/persistence test passed\n";
    return 0;
}
