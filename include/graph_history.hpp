#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "config.hpp"
#include "vectors.hpp"

struct GraphSample {
    std::size_t step = 0;
    bool starts_segment = false;

    std::array<Vec3, NUM_CELESTIAL_BODIES> position, velocity, acceleration;
    std::array<double, NUM_CELESTIAL_BODIES> mass;

    double energy = 0, energy_drift = 0, kinetic_energy = 0;
    Vec3 momentum{}, momentum_drift{}, barycenter_drift{};
    bool barycenter_valid = false;
};

class GraphHistory {
    mutable std::mutex mtx;
    std::vector<GraphSample> samples;
    std::size_t every_steps = 1;
    std::uint64_t generation = 0;

public:
    static constexpr std::size_t CAPACITY = 8192;

    bool due(const std::size_t step) const {
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

            generation++;  // tells the UI to rebuild its series
        }
    }

    void clear() {
        std::lock_guard lock(mtx);
        samples.clear();
        every_steps = 1;
        generation++;
    }

    template <class Reader>
    void read(Reader&& reader) const {
        // Returning a reference would be unsafe (caller uses the vector unprotected)
        // Returning a copy would be too expensive

        std::lock_guard lock(mtx);
        reader(std::span<const GraphSample>(samples), generation, every_steps);
    }
};
