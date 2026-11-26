#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "config.hpp"
#include "hierarchy.hpp"
#include "utils.hpp"
#include "vectors.hpp"

struct GraphSample {
    step_t step = 0;
    bool starts_segment = false;

    std::array<Vec3, NUM_CELESTIAL_BODIES> position, velocity, acceleration;
    std::array<double, NUM_CELESTIAL_BODIES> mass;

    double energy = 0, energy_drift = 0, kinetic_energy = 0;
    Vec3 momentum{}, momentum_drift{}, barycenter_drift{};
    bool barycenter_valid = false;

    Hierarchy hierarchy;
    std::array<std::uint64_t, NUM_CELESTIAL_BODIES> body_id{};
    // NO_BODY_ID for disabled slots; Slots get reused, IDs never
};

class GraphHistory {
    mutable std::mutex mtx;
    std::vector<GraphSample> samples;
    step_t every_steps = 1;
    std::uint64_t generation = 0;

public:
    static constexpr std::size_t CAPACITY = 16'384; // TODO: Make this changeable in the UI

    bool due(const step_t step) const {
        return step % every_steps == 0;
    }

    void record(const GraphSample& sample) {
        std::lock_guard lock(mtx);
        samples.push_back(sample);

        if (samples.size() >= CAPACITY) {
            every_steps *= 2;
            std::erase_if(samples, [&](const GraphSample& s) {
                return !s.starts_segment && s.step % every_steps != 0;
            });

            if (samples.size() >= CAPACITY) {
                samples.erase(samples.begin(), samples.begin() + CAPACITY / 2);
            }

            generation++; // tells the UI to rebuild its series
        }
    }

    void clear() {
        std::lock_guard lock(mtx);
        samples.clear();
        every_steps = 1;
        generation++;
    }

    // reader(samples, generation, every_steps) runs while the history is locked
    // Don't call graph_history inside the lambda: std::mutex isn't recursive, so it would deadlock
    void read(std::invocable<std::span<const GraphSample>, std::uint64_t, step_t> auto&& reader) const {
        // Returning a reference would be unsafe (used after unlocking -> data race)
        // Returning a copy would be too expensive (~113.9 MB at max capacity)

        std::lock_guard lock(mtx);
        reader(std::span(samples), generation, every_steps);
    }
};
