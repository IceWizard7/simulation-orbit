#include "simulation.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

#include "ui.hpp"

CelestialBody simulation::celestial_bodies[NUM_CELESTIAL_BODIES] = {
    {"Sun", {5.254258484891016e8, -8.691526594804802e8, -1.013312408460682e7}, {1.486418097392948e1, 1.867348026651063e0, -4.030379227978539e-1}, 1.3271244004193937e+20, 10, 0.03, (Color){255, 230,  40, 255}, 90560.0 * 86'400, "resources/sun.jpg"},
    {"Mercury", {-3.863295206424535e10, 3.095205136713375e10, 6.195274889256019e9}, {-4.061253084483177e4, -3.574724011520127e4, 8.385974494151380e2}, 22031868550000.0, 5, 0.01, (Color){150, 150, 150, 255}, 87.969 * 86'400, "resources/mercury.jpg"},
    {"Venus", {-9.442641552608661e10, 4.918920270156867e10, 6.142101433096975e9}, {-1.646745669837079e4, -3.114773688845666e4, 5.450657096441862e2}, 324858592000000.0, 5, 0.01, (Color){245, 190,  70, 255}, 224.701 * 86'400, "resources/venus.jpg"},
    {"Earth", {-3.821000604658472e10, 1.410274684528763e11, 5.275940805160999e7}, {-2.920909465286638e4, -7.960531594616490e3, -6.079433916260868e0}, 398600435436000.0, 5, 0.01, (Color){ 40, 120, 204, 255}, 365.256 * 86'400, "resources/earth.jpg", 0.00108263, 6378136.6, {0.00000000, 0.39777716, 0.91748206}},
    {"Mars", {-1.603072902891413e11, -1.693943532268408e11, 4.939584574298635e8}, {1.848810083914610e4, -1.467137544186248e4, -7.687147050137604e2}, 42828375662000.0, 10, 0.02, (Color){220,  60,  40, 255}, 686.980 * 86'400, "resources/mars.jpg", 0.00196045, 3396190.0, {0.44615053, -0.05550828, 0.89323487}},
    {"Jupiter", {-6.174066213916292e9, 7.669935543317993e11, -2.919675836705565e9}, {-1.321414969549137e4, 4.963533982492354e2, 2.948696326167778e2}, 1.266865319e+17, 10, 0.15, (Color){220, 150,  85, 255}, 4332.589 * 86'400, "resources/jupiter.jpg", 0.014736, 71492000.0, {-0.01459926, -0.03581847, 0.99925167}},
    {"Saturn", {-8.515017102896239e11, 1.061724056177945e12, 1.475763099241823e10}, {-8.067709138805704e3, -6.064681250795719e3, 4.267881978436692e2}, 3.7931206234e+16, 10, 0.15, (Color){235, 205, 120, 255}, 10759.22 * 86'400, "resources/saturn.jpg", 0.016298, 60268000.0, {0.08547883, 0.46244168, 0.88251972}},
    {"Uranus", {-2.732875467120085e12, 1.447663723744908e11, 3.619086410308249e10}, {-4.165944517727502e2, -7.115869467058308e3, -2.130718237900275e1}, 5793950610300000.0, 10, 0.15, (Color){ 80, 220, 220, 255}, 30685.4 * 86'400, "resources/uranus.jpg", 0.00334343, 25559000.0, {-0.21199958, -0.96798900, 0.13436320}},
    {"Neptune", {-3.037152106379823e12, -3.366575290468045e12, 1.392493881819084e11}, {4.002400188245791e3, -3.608893091439345e3, -1.753848336826191e1}, 6835099970000000.0, 10, 0.15, (Color){ 40,  80, 230, 255}, 60189.0 * 86'400, "resources/neptune.jpg", 0.003411, 24764000.0, {0.35588326, -0.30681030, 0.88273130}},
    {"Pluto", {5.435493283758588e12, -2.498597656155004e12, -1.304284834205698e12}, {2.629139130853487e3, 3.571607925259364e3, -1.120892268633253e3}, 869326000000.0, 10, 0.15, (Color){185, 155, 130, 255}, 90560.0 * 86'400},
    {"Moon", {-3.782417827609404e10, 1.411362073094813e11, 3.993214356346428e7}, {-2.943549761633117e4, -7.016310635821375e3, 7.358756789024357e1}, 4902800066000.0, 5, 0.01, (Color){200, 200, 200, 255}, 27.322 * 86'400, "resources/moon.jpg"},
    {"Phobos", {-1.603144727827997e11, -1.693895510926101e11, 4.979424081231654e8}, {1.748181894901772e4, -1.648636466442317e4, -4.084555018062686e2}, 708754.6066894452, 5, 0.01, (Color){105, 93, 82, 255}, 0.3189 * 86'400},
    {"Deimos", {-1.603285635818097e11, -1.693959085793375e11, 5.037242108345851e8}, {1.852373428359592e4, -1.601519698639884e4, -9.046220102407760e2}, 96155.69648120314, 5, 0.01, (Color){139, 118, 99, 255}, 1.2624 * 86'400},
    {"Io", {-6.233966160638074e9, 7.665746591434332e11, -2.935518177157819e9}, {3.880765477003187e3, -1.934781545909256e3, 4.632707592579830e2}, 5959915500000.0, 5, 0.01, (Color){240, 197, 79, 255}, 1.7691 * 86'400, "resources/io.jpg"},
    {"Europa", {-5.524824063946516e9, 7.668193677270226e11, -2.921204185672104e9}, {-9.538034154701794e3, 1.370099415341497e4, 8.680637549578591e2}, 3202712100000.0, 5, 0.01, (Color){210, 199, 174, 255}, 3.5512 * 86'400, "resources/europa.jpg"},
    {"Ganymede", {-5.723547525943480e9, 7.660214110618627e11, -2.944168016637921e9}, {-3.356350373049795e3, 5.063255070955623e3, 5.911561261992728e2}, 9887832800000.0, 5, 0.01, (Color){139, 126, 112, 255}, 7.1546 * 86'400, "resources/ganymede.jpg"},
    {"Callisto", {-6.727058694929393e9, 7.687975581029507e11, -2.865250786776066e9}, {-2.101874814219247e4, -1.952231661511678e3, 1.802771219350360e2}, 7179283400000.0, 5, 0.01, (Color){76, 66, 60, 255}, 16.689 * 86'400, "resources/callisto.jpg"},
    {"Mimas", {-8.516636581629769e11, 1.061807980517000e12, 1.472360319106400e10}, {-1.474897014376366e4, -1.697383155305549e4, 6.853184301229338e3}, 2503489000.0, 5, 0.01, (Color){207, 208, 197, 255}, 0.942 * 86'400},
    {"Enceladus", {-8.515107031148618e11, 1.061512709436290e12, 1.486921881305206e10}, {4.450800374170360e3, -6.989483977905879e3, -2.979779046934832e2}, 7210367000.0, 5, 0.01, (Color){231, 240, 242, 255}, 1.3702 * 86'400},
    {"Tethys", {-8.512188824985989e11, 1.061642053707790e12, 1.476698511971587e10}, {-5.115510688254963e3, 3.511397721990353e3, -4.909965695299988e3}, 41210000000.0, 5, 0.01, (Color){202, 203, 191, 255}, 1.8878 * 86'400},
    {"Dione", {-8.518781555267210e11, 1.061732633450093e12, 1.478965692075050e10}, {-7.858318030862320e3, -1.494278928583686e4, 5.057131541893256e3}, 73116000000.0, 5, 0.01, (Color){190, 190, 180, 255}, 2.7369 * 86'400},
    {"Rhea", {-8.512081243267576e11, 1.061324823121111e12, 1.493666597036761e10}, {-1.049824839418538e3, -2.120834237853587e3, -2.262008728035155e3}, 153940000000.0, 5, 0.01, (Color){180, 180, 170, 255}, 4.5175 * 86'400},
    {"Titan", {-8.522235383588899e11, 1.062636909412597e12, 1.435545804601282e10}, {-1.243404316489154e4, -8.910715502593630e3, 2.274078165657974e3}, 8978140000000.0, 5, 0.01, (Color){201, 141, 71, 255}, 15.9454 * 86'400, "resources/titan.jpg"},
    {"Hyperion", {-8.498936584797186e11, 1.061657170549778e12, 1.463885077849942e10}, {-8.302698690947226e3, -1.957528662676872e3, -1.687587676827684e3}, 370500000.0, 5, 0.01, (Color){136, 99, 70, 255}, 21.2766 * 86'400},
    {"Iapetus", {-8.500748912276926e11, 1.058599343338804e12, 1.540162810358328e10}, {-5.123261858488538e3, -4.828112583515693e3, -4.944844931578785e2}, 120520000000.0, 5, 0.01, (Color){154, 137, 108, 255}, 79.3215 * 86'400},
    {"Phoebe", {-8.596026791222472e11, 1.074004499023409e12, 1.536507341007924e10}, {-6.776370060222495e3, -5.315086922292050e3, 4.729529789854172e2}, 554800000.0, 5, 0.01, (Color){71, 68, 67, 255}, 550.31 * 86'400},
    {"Ariel", {-2.732926592440873e12, 1.448027471019274e11, 3.637137283902641e10}, {4.757137601380178e3, -8.020697343188287e3, 1.624044551554098e3}, 83430000000.0, 5, 0.01, (Color){188, 203, 205, 255}, 2.5204 * 86'400},
    {"Umbriel", {-2.732675563041831e12, 1.447467928691133e11, 3.636333632755692e10}, {2.493028286681002e3, -8.245372951301391e3, -3.520939634330069e3}, 85400000000.0, 5, 0.01, (Color){79, 85, 91, 255}, 4.1442 * 86'400},
    {"Titania", {-2.732787677630899e12, 1.448064216307254e11, 3.661602093365331e10}, {3.070103306269134e3, -7.968837078556122e3, -6.678648147948683e2}, 222800000000.0, 5, 0.01, (Color){150, 153, 160, 255}, 8.7059 * 86'400},
    {"Oberon", {-2.732558933353387e12, 1.447666066918093e11, 3.668055480000018e10}, {2.150769994181039e3, -7.902392191027546e3, -1.675388617524444e3}, 205340000000.0, 5, 0.01, (Color){102, 97, 92, 255}, 13.4632 * 86'400},
    {"Miranda", {-2.732777161966636e12, 1.447492623984428e11, 3.627381035799313e10}, {3.688720362717225e3, -8.339561536683586e3, -5.153670218630429e3}, 4300000000.0, 5, 0.01, (Color){169, 174, 163, 255}, 1.4135 * 86'400},
    {"Triton", {-3.036849327642753e12, -3.366751911600176e12, 1.391945940467093e11}, {1.719779322935817e3, -7.085360285245934e3, -1.421920964079227e3}, 1428495000000.0, 5, 0.01, (Color){194, 190, 183, 255}, 5.8769 * 86'400, "resources/triton.jpg"},
    {"Nereid", {-3.038398735205438e12, -3.359192069808491e12, 1.397450502344244e11}, {3.562841709325018e3, -4.230299935844265e3, -8.167482805650472e1}, 2070000000.0, 5, 0.01, (Color){164, 171, 174, 255}, 360.13 * 86'400},
    {"Proteus", {-3.037148682780139e12, -3.366463893257564e12, 1.392870454662285e11}, {-3.159672291805094e3, -4.248840637528186e3, 2.515053078162541e3}, 2580000000.0, 5, 0.01, (Color){89, 91, 89, 255}, 1.1223 * 86'400},
    {"Charon", {5.435507571739943e12, -2.498585169590914e12, -1.304289714788928e12}, {2.649794155054928e3, 3.470637020560921e3, -1.318807330374422e3}, 106100000000.0, 5, 0.01, (Color){126, 117, 109, 255}, 6.3872 * 86'400},
    {"Nix", {5.435506231426013e12, -2.498562926832713e12, -1.304251732310032e12}, {2.730701004502762e3, 3.613724687258064e3, -1.230248067585195e3}, 1496000.0, 5, 0.01, (Color){184, 183, 175, 255}, 24.856 * 86'400},
    {"Hydra", {5.435541905260762e12, -2.498564246715345e12, -1.304315503974369e12}, {2.619335786799016e3, 3.486905755936516e3, -1.240844499188735e3}, 2010000.0, 5, 0.01, (Color){164, 165, 157, 255}, 38.202 * 86'400},
    {"Kerberos", {5.435454730070845e12, -2.498637906440255e12, -1.304282146787419e12}, {2.598784079825089e3, 3.601093273190158e3, -1.023177596881631e3}, 60380.0, 5, 0.01, (Color){84, 80, 78, 255}, 32.168 * 86'400},
};

namespace {
    constexpr std::array earth_moons{10};
    constexpr std::array mars_moons{11, 12};
    constexpr std::array jupiter_moons{13, 14, 15, 16};
    constexpr std::array saturn_moons{17, 18, 19, 20, 21, 22, 23, 24, 25};
    constexpr std::array uranus_moons{26, 27, 28, 29, 30};
    constexpr std::array neptune_moons{31, 32, 33};
    constexpr std::array pluto_moons{34, 35, 36, 37};
}

const std::array<PlanetarySystem, 7> simulation::planetary_systems{{
    {3, earth_moons},
    {4, mars_moons},
    {5, jupiter_moons},
    {6, saturn_moons},
    {7, uranus_moons},
    {8, neptune_moons},
    {9, pluto_moons},
}};

void simulation::apply_planet_systems_approximation() {
    for (const auto&[parent_index, moon_indices] : planetary_systems) {
        auto& parent = celestial_bodies[parent_index];

        double total_gravitational_mass = parent.original_gravitational_mass;
        Vec3 weighted_position = parent.position * parent.original_gravitational_mass;
        Vec3 weighted_velocity = parent.velocity * parent.original_gravitational_mass;

        for (const int moon_index : moon_indices) {
            auto& moon = celestial_bodies[moon_index];
            total_gravitational_mass += moon.original_gravitational_mass;
            weighted_position += moon.position * moon.original_gravitational_mass;
            weighted_velocity += moon.velocity * moon.original_gravitational_mass;
            moon.enabled = false;
        }

        parent.position = weighted_position / total_gravitational_mass;
        parent.velocity = weighted_velocity / total_gravitational_mass;
        parent.gravitational_mass = total_gravitational_mass;
    }

    accelerations_at_current_positions.reset();
}

std::ofstream simulation::live_csv_file;
std::optional<std::size_t> simulation::last_csv_sample_step;
bool simulation::csv_output_initialized = false;
bool simulation::live_csv_write_failed = false;

CSVEntry simulation::capture_csv_entry() {
    CSVEntry entry;
    entry.step = config::steps_simulated.load();
    entry.simulated_seconds = entry.step * runtime_config::time_step;

    for (int i = 0; i < NUM_CELESTIAL_BODIES; ++i) {
        if (!celestial_bodies[i].enabled) continue;
        entry.positions[i] = celestial_bodies[i].position;
        entry.velocities[i] = celestial_bodies[i].velocity;
    }

    return entry;
}

void simulation::write_csv_header(std::ostream& output) {
    output << "step,";
    output << "simulated_seconds";
    for (const auto& celestial_body : celestial_bodies) {
        if (!celestial_body.enabled) continue;
        output << ',' << celestial_body.name << "_position_x"
               << ',' << celestial_body.name << "_position_y"
               << ',' << celestial_body.name << "_position_z"
               << ',' << celestial_body.name << "_velocity_x"
               << ',' << celestial_body.name << "_velocity_y"
               << ',' << celestial_body.name << "_velocity_z";
    }
    output << '\n';
}

void simulation::write_csv_entry(std::ostream& output, const CSVEntry& entry) {
    output << entry.step << "," << entry.simulated_seconds;

    for (int i = 0; i < NUM_CELESTIAL_BODIES; ++i) {
        const auto& position = entry.positions[i];
        const auto& velocity = entry.velocities[i];

        if (!position.has_value()) continue;
        if (!velocity.has_value()) continue;

        output << ',' << position->x
               << ',' << position->y
               << ',' << position->z
               << ',' << velocity->x
               << ',' << velocity->y
               << ',' << velocity->z;
    }

    output << '\n';
}

bool simulation::report_live_csv_write_failure(const std::size_t step) {
    if (!live_csv_write_failed) {
        std::cerr << "Error: Failed to write CSV sample at step " << step
                  << " to " << *runtime_config::csv_path << ".\n";
        live_csv_write_failed = true;
    }
    return false;
}

bool simulation::record_csv_sample(const bool force) {
    if (!csv_output_initialized || !runtime_config::csv_path.has_value()) return true;
    if (live_csv_write_failed) return false;

    const std::size_t step = config::steps_simulated.load();
    if (last_csv_sample_step.has_value() && *last_csv_sample_step == step) return true;

    if (!force) {
        if (!runtime_config::sample_csv_data_every_steps.has_value()) return true;

        const std::size_t cadence = *runtime_config::sample_csv_data_every_steps;
        if (cadence == 0 || step % cadence != 0) return true;
    }

    const CSVEntry entry = capture_csv_entry();
    if (runtime_config::csv_live) {
        write_csv_entry(live_csv_file, entry);
        live_csv_file.flush();
        if (!live_csv_file) return report_live_csv_write_failure(step);
    } else {
        csv_data.push_back(entry);
    }

    last_csv_sample_step = step;
    return true;
}

void simulation::initialize_orbit_sampling() {
    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const double seconds_per_point_target = celestial_bodies[i].orbital_period_seconds / config::ORBIT_POINTS_PER_REVOLUTION;

        orbit_sample_every_steps[i] = std::max<std::size_t>(
            1,
            static_cast<std::size_t>(seconds_per_point_target / runtime_config::time_step)
        );

        const double seconds_per_point = static_cast<double>(orbit_sample_every_steps[i]) * runtime_config::time_step;

        orbit_max_points[i] = std::max<std::size_t>(
            2,
            static_cast<std::size_t>(std::ceil(
                config::ORBIT_TAIL_REVOLUTIONS * celestial_bodies[i].orbital_period_seconds / seconds_per_point
            ))
        );
    }

    // Seed every history with the step-0 state so all bodies' coverage starts together;
    // otherwise coarse-cadence bodies would have no samples until their first cadence multiple
    // and publish_snapshot would truncate other trails against a late-starting center history
    save_orbit_points();
}

void simulation::save_orbit_points() {
    const std::size_t step = config::steps_simulated.load();

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        if (step % orbit_sample_every_steps[i] != 0) continue;

        auto& history = orbit_history[i];

        history.push_back(celestial_bodies[i].position);

        if (history.size() > orbit_max_points[i]) {
            history.pop_front();
        }
    }
}

bool simulation::initialize_csv_output() {
    csv_data.clear();
    last_csv_sample_step.reset();
    live_csv_write_failed = false;
    csv_output_initialized = false;

    const bool has_path = runtime_config::csv_path.has_value();
    const bool has_cadence = runtime_config::sample_csv_data_every_steps.has_value();
    if (!has_path && !has_cadence && !runtime_config::csv_live) return true;

    if (!has_path || !has_cadence || *runtime_config::sample_csv_data_every_steps == 0) {
        std::cerr << "Error: CSV output requires a path and a positive sampling cadence.\n";
        return false;
    }

    if (runtime_config::csv_live) {
        live_csv_file.open(*runtime_config::csv_path, std::ios::out | std::ios::trunc);
        if (!live_csv_file.is_open()) {
            std::cerr << "Error: Could not open CSV file " << *runtime_config::csv_path << ".\n";
            return false;
        }

        live_csv_file.precision(std::numeric_limits<double>::max_digits10);
        write_csv_header(live_csv_file);
        if (!live_csv_file) {
            std::cerr << "Error: Failed to write CSV header to " << *runtime_config::csv_path << ".\n";
            live_csv_write_failed = true;
            live_csv_file.close();
            return false;
        }
    }

    csv_output_initialized = true;
    if (record_csv_sample(true)) return true;

    if (live_csv_file.is_open()) live_csv_file.close();
    csv_output_initialized = false;
    return false;
}

void simulation::update_csv_data() {
    record_csv_sample(false);
}

std::array<Vec3, NUM_CELESTIAL_BODIES> simulation::compute_accelerations() {
    std::array<Vec3, NUM_CELESTIAL_BODIES> accelerations = {};

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
            if (!celestial_bodies[j].enabled) continue;
            if (i == j) continue; // do not apply gravity from this object to this
            accelerations[i] += celestial_bodies[i].acceleration_due_to(celestial_bodies[j]);
        }
    }

    return accelerations;
}

void simulation::simulate_step() {
    if (!accelerations_at_current_positions.has_value()) {
        accelerations_at_current_positions = compute_accelerations();
    }

    const std::array<Vec3, NUM_CELESTIAL_BODIES>& accelerations = *accelerations_at_current_positions;

    // Velocity Verlet: keep the orbit phase stable over many short-period inner-planet revolutions
    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        celestial_bodies[i].position += celestial_bodies[i].velocity * runtime_config::time_step
            + accelerations[i] * runtime_config::half_dt_squared;
    }

    const std::array<Vec3, NUM_CELESTIAL_BODIES> next_accelerations = compute_accelerations();

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        celestial_bodies[i].velocity += (accelerations[i] + next_accelerations[i]) * runtime_config::half_dt;
    }

    // accelerations_at_current_positions now correspond to next_accelerations
    accelerations_at_current_positions = next_accelerations;

    ++config::steps_simulated;
    save_orbit_points();
    update_csv_data();
}

// Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
// of celestial_bodies & orbit_history) and publishes it atomically
void simulation::publish_snapshot() {
    auto snap = std::make_shared<ui::RenderSnapshot>();

    const std::size_t current_step = config::steps_simulated.load();
    const int center_index = config::center_celestial_body_index;
    const Vec3 center = celestial_bodies[center_index].position;
    const auto& center_history = orbit_history[center_index];
    const std::size_t center_cadence = orbit_sample_every_steps[center_index];
    const std::size_t center_last_step = current_step / center_cadence * center_cadence;
    // Oldest step for which the center's position is known. Trail samples older than this have no
    // co-moving frame: subtracting a stale center position would draw a ghost of the body's
    // barycentric orbit on top of its relative trail, so publish truncates them instead
    const std::size_t center_first_step = center_history.empty()
        ? current_step
        : center_last_step - (center_history.size() - 1) * center_cadence;

    // Per-body cadences are not index-aligned across bodies, so the center position belonging to a
    // body sample is interpolated between the center's own samples by step number
    auto center_position_at_step = [&](const std::size_t step) -> Vec3 {
        if (center_history.empty()) return center;

        if (step <= center_first_step) return center_history.front();

        if (step >= center_last_step) {
            // Between the center's newest sample and its current position
            if (current_step == center_last_step) return center_history.back();
            const double t = static_cast<double>(step - center_last_step)
                / static_cast<double>(current_step - center_last_step);
            return center_history.back() + (center - center_history.back()) * t;
        }

        const std::size_t offset = step - center_first_step;
        const std::size_t k = offset / center_cadence;
        const double t = static_cast<double>(offset - k * center_cadence) / static_cast<double>(center_cadence);
        return center_history[k] + (center_history[k + 1] - center_history[k]) * t;
    };

    std::size_t max_used = 0;

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        const auto& body = celestial_bodies[i];

        Vec3 pos = body.position;
        if (config::RENDERING_COORDINATES_RELATIVE_TO_OBJECT) {
            pos -= center;
        }
        snap->bodies[i] = {body.name, pos, body.draw_radius_2d, body.draw_radius_3d, body.color, &body.planet_visual, body.enabled};

        const auto& history = orbit_history[i];
        max_used = std::max(max_used, history.size());

        // Don't include center in snapshot
        if constexpr (config::RENDERING_COORDINATES_RELATIVE_TO_OBJECT) {
            if (i == center_index) continue;
        }
        if (history.size() < 2 || !body.color.has_value()) continue;

        snap->orbits[i].color = body.color;

        const std::size_t cadence = orbit_sample_every_steps[i];
        const std::size_t last_step = current_step / cadence * cadence;
        const std::size_t first_step = last_step - (history.size() - 1) * cadence;

        // Skip samples older than the center's history (no co-moving frame exists for them)
        std::size_t first_rendered = 0;
        if (first_step < center_first_step) {
            first_rendered = (center_first_step - first_step + cadence - 1) / cadence;
            if (first_rendered > history.size()) first_rendered = history.size();
        }

        snap->orbits[i].points.reserve(history.size() - first_rendered + 1);

        for (std::size_t j = first_rendered; j < history.size(); j++) {
            const std::size_t step = last_step - (history.size() - 1 - j) * cadence;
            snap->orbits[i].points.push_back(history[j] - center_position_at_step(step));
        }

        // Connect the trail head to the body's current position
        snap->orbits[i].points.push_back(body.position - center);
    }

    snap->max_orbit_points_used = max_used;

    const int planet_info_display_index = config::planet_info_display_index; // load once

    if (planet_info_display_index != -1) {
        const auto& body = celestial_bodies[planet_info_display_index];
        snap->detailed_body_display = {body.name, body.position, body.velocity, body.get_mass(), body.color};
    }

    {
        std::lock_guard lock(config::snapshot_lock);
        config::latest_snapshot = std::move(snap);
    }
}

void simulation::simulate_cpu(const std::stop_token& stop_token) {
    auto last_publish = std::chrono::steady_clock::now();
    auto last_budget_update = std::chrono::steady_clock::now();
    double step_budget = 0.0;
    std::size_t since_check = 0;

    auto publish_if_due = [&] {
        const auto now = std::chrono::steady_clock::now();
        // publish at 1.2x the target FPS (smoother)
        if (now - last_publish >= std::chrono::duration<double>(1.0 / (config::TARGET_FPS * 1.2))) {
            publish_snapshot();
            last_publish = now;
        }
    };

    while (!stop_token.stop_requested()) {
        // If the **animation** appears laggy / slow, try lowering this
        // It does not affect CPU simulation speed
        constexpr std::size_t MAX_STEPS_PER_BATCH = 4096;
        constexpr double MAX_CATCH_UP_SECONDS = 0.25;
        if (!config::timer.is_running()) {
            step_budget = 0.0;
            last_budget_update = std::chrono::steady_clock::now();

            if (config::republish_needed) {
                config::republish_needed = false;
                publish_snapshot();
                last_publish = std::chrono::steady_clock::now();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config::TARGET_FPS));
            continue;
        }

        if (runtime_config::target_steps.has_value()
            && config::steps_simulated.load() >= *runtime_config::target_steps) {
            config::timer.pause();
            publish_snapshot();
            last_publish = std::chrono::steady_clock::now();
            continue;
        }

        if (runtime_config::target_simulation_speed <= 0.0) {
            simulate_step();

            if (++since_check >= MAX_STEPS_PER_BATCH) {
                since_check = 0;
                publish_if_due();
            }

            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_budget_update).count();
        last_budget_update = now;

        double max_budget = std::max(
            1.0,
            runtime_config::target_steps_per_second * MAX_CATCH_UP_SECONDS
        );

        step_budget = std::min(
            step_budget + elapsed * runtime_config::target_steps_per_second,
            max_budget
        );

        const auto steps_to_run = static_cast<std::size_t>(
            std::min(step_budget, static_cast<double>(MAX_STEPS_PER_BATCH))
        );

        if (steps_to_run == 0) {
            const double seconds_to_next_step =
                (1.0 - step_budget) / runtime_config::target_steps_per_second;

            std::this_thread::sleep_for(std::chrono::duration<double>(
                std::min(seconds_to_next_step, 1.0 / config::TARGET_FPS)
            ));
            continue;
        }

        for (std::size_t i = 0; i < steps_to_run; ++i) {
            simulate_step();
        }

        step_budget -= static_cast<double>(steps_to_run);
        publish_if_due();
    }
}

bool simulation::finalize_csv_output() {
    if (!csv_output_initialized) return true;

    bool write_succeeded = record_csv_sample(true);

    if (runtime_config::csv_live) {
        if (live_csv_file.is_open()) {
            live_csv_file.close();
            if (!live_csv_file && !live_csv_write_failed) {
                std::cerr << "Error: Failed to finish writing CSV data to "
                          << *runtime_config::csv_path << ".\n";
                write_succeeded = false;
            }
        }

        csv_output_initialized = false;
        return write_succeeded && !live_csv_write_failed;
    }

    std::ofstream csv_file(*runtime_config::csv_path, std::ios::out | std::ios::trunc);
    if (!csv_file.is_open()) {
        std::cerr << "Error: Could not open CSV file " << *runtime_config::csv_path << ".\n";
        csv_output_initialized = false;
        return false;
    }

    // Preserve enough digits to reconstruct the original double values.
    csv_file.precision(std::numeric_limits<double>::max_digits10);
    write_csv_header(csv_file);
    for (const auto& entry : csv_data) {
        write_csv_entry(csv_file, entry);
    }

    csv_file.close();
    if (!csv_file) {
        std::cerr << "Error: Failed to write CSV data to " << *runtime_config::csv_path << ".\n";
        write_succeeded = false;
    }

    csv_output_initialized = false;
    return write_succeeded;
}

void simulation::print_final_state() {
    const double seconds = config::timer.seconds();

    printf("Simulated years per second: %.2f\n", seconds > 0.0 ? ui::simulated_years() / seconds : 0);
    printf("Computation time: %.2f seconds\n", seconds);
    printf("Simulation time: %.8f years (@ 365 days)\n", ui::simulated_years());
    printf("Steps simulated: %zu\n", config::steps_simulated.load());
    printf("Time step: %d seconds\n", runtime_config::time_step);
    printf("Body set: %s\n", runtime_config::body_set_name());
    printf("Invocation command: %s\n", runtime_config::invocation_command.c_str());

    printf("Enabled body indices: ");
    bool first_enabled_body = true;
    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies[i].enabled) continue;
        if (!first_enabled_body) printf(",");
        printf("%d", i);
        first_enabled_body = false;
    }
    printf("\n");

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& celestial_body = celestial_bodies[i];
        printf("%s", celestial_body.position.to_exact_string().c_str());
        if (i != NUM_CELESTIAL_BODIES - 1) {
            printf("|");
        }
    }
    printf("\n");
}
