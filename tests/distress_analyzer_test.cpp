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
    DistressAnalyzer analyzer({0.3f, 1.0f, 2.0f, 0.18f, 2.0f});
    const auto first = analyzer.evaluate(track, no_pose, second);
    assert(first.potential);
    assert(first.score >= 0.35f);
    assert(first.reason.find("bobbing") != std::string::npos);
    assert(!first.alerting);

    const auto held = analyzer.evaluate(track, no_pose, 3 * second);
    assert(held.potential);
    assert(held.alerting);

    std::cout << "distress analyzer bobbing/persistence test passed\n";
    return 0;
}
