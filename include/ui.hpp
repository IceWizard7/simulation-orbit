#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <raylib.h>
#include <utility>
#include <variant>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "config.hpp"   // IWYU pragma: export
#include "utils.hpp"    // IWYU pragma: export
#include "vectors.hpp"  // IWYU pragma: export
#include "hierarchy.hpp"
#include "orbital_elements.hpp"
#include "planet_visual.hpp"

namespace ui_data {
    struct DiagnosticsBaseline {
        double total_energy = 0.0;
        Vec3 linear_momentum{};
        Vec3 barycenter{};
        step_t steps_simulated = 0;
    };

    struct CheckpointInfo {
        step_t id = 0;
        double simulated_seconds = 0;
        str label;
    };

    // Immutable, render-ready view of the system
    // Producer: simulation thread, Consumer: renderer thread
    // Positions & orbit points: Relative to the center body => renderer thread can re-project them under the current zoom every frame
    struct RenderSnapshot {
        struct Body {
            std::uint64_t id = 0;
            str name;
            Vec3 relative_position;
            Vec3 absolute_position;
            Vec3 velocity;
            double mass;
            double physical_radius_m = 0;
            float radius_2d = 0;
            float radius_3d = 0;
            std::optional<Color> color;
            std::optional<std::size_t> texture_id;
            bool rendering_visible = false;

            double gravitational_parameter = 0; // GM (m^3/s^2), the model parameter; mass = GM / G
            double j2 = 0;
            double j2_reference_radius_m = 0; // Equatorial reference radius of the J2 term (!= physical_radius_m)
            Vec3 pole_axis{}; // Unit spin axis, stored J2000 ecliptic frame
        };

        struct Orbit {
            std::vector<Vec3> points;
            std::optional<Color> color;
        };

        std::array<std::optional<Body>, NUM_CELESTIAL_BODIES> bodies;
        std::array<Orbit, NUM_CELESTIAL_BODIES> orbits;
        std::size_t max_orbit_points_used = 0;
        std::vector<CheckpointInfo> checkpoints;

        Vec3 linear_momentum{};
        Vec3 baseline_linear_momentum{};
        Vec3 momentum_drift{};

        double total_energy = 0;
        double baseline_energy = 0.0;
        double energy_drift = 0.0;

        Vec3 barycenter{};
        Vec3 expected_barycenter{};
        Vec3 barycenter_drift{};
        bool barycenter_valid = false;
        Hierarchy hierarchy;

        std::size_t center_index = config::NO_BODY_SELECTED;
        step_t step = 0; // Step at which this state was captured (consistent with all body fields)
    };

    struct EditableDouble {
        double val = 0.0;
        bool editing = false;
        bool dirty = false; // contains an unsubmitted change
    };

    struct BodyEditState {
        EditableDouble physical_radius_m;
        EditableDouble mass;
        EditableDouble px, py, pz;
        EditableDouble vx, vy, vz;

        float color[3]{}; // Picker draft; Follows the snapshot unless the picker is being dragged
        bool color_active = false; // ColorEdit3 was active last frame
        str rename_buffer; // Only valid while the rename popup is open

        std::optional<std::size_t> reference_index;
    };

    struct UpdateBodyCommand {
        std::size_t i;
        std::optional<std::size_t> reference_index;

        std::optional<double> mass;
        std::optional<double> px, py, pz;
        std::optional<double> vx, vy, vz;

        std::optional<str> name;
        std::optional<double> physical_radius_m;
        std::optional<Color> color;

        bool disable_rendering = false;
        bool enable_rendering = false;

        std::optional<std::size_t> texture_id; // Set
        bool clear_texture = false; // Remove (back to color)
    };

    struct CreateBodyCommand {
        std::optional<std::size_t> reference_index;
        Vec3 position{}, velocity{};
        double mass;
        double physical_radius_m = 0;
        str name;
        Color color = BLACK;
        std::optional<std::size_t> texture_id; // Default: drawn with color instead of texture
    };

    struct DeleteBodyCommand {
        std::size_t i;
    };

    struct CaptureCheckpoint {
    };

    struct RestoreCheckpoint {
        step_t id;
    };

    struct DeleteCheckpoint {
        step_t id;
    };

    struct RenameCheckpoint {
        step_t id;
        str new_label;
    };

    struct StepN {
        step_t n;
    };

    struct ClearGraphs {
    };

    struct Oblateness {
        double j2 = 0;
        double reference_radius_m = 0;
        Vec3 pole{}; // Unit vector, stored J2000 ecliptic frame
    };

    // 3 JSON states per optional field: Key missing -> catalog (custom: default); null -> explicitly none; value
    struct ImportedBody {
        std::optional<std::size_t> catalog_index; // std::nullopt: Custom body
        str name;
        double gravitational_parameter = 0;
        Vec3 position, velocity;

        std::optional<double> radius_m;
        std::optional<std::optional<Oblateness>> oblateness;
        std::optional<std::optional<Color>> color;
        bool visible = true;

        // Parsed: missing = catalog/custom default, std::nullopt inside: explicitly none, string: file to load
        std::optional<std::optional<str>> texture_path;

        // Resolved on the renderer thread by import_state_text (GL upload); the simulation thread only reads this
        std::optional<std::optional<std::size_t>> texture_id;
    };

    struct ImportStateCommand {
        std::vector<ImportedBody> bodies;
        double simulated_seconds = 0;
        str label; // Checkpoint label, ex. "Imported: state_step123.json"
    };

    using SimulationCommand = std::variant<
        UpdateBodyCommand,

        CaptureCheckpoint,
        RestoreCheckpoint,
        ImportStateCommand,
        DeleteCheckpoint,
        RenameCheckpoint,

        StepN,
        ClearGraphs,

        CreateBodyCommand,
        DeleteBodyCommand
    >;

    inline std::array<BodyEditState, NUM_CELESTIAL_BODIES> edit_states{};
    inline std::array<bool, NUM_CELESTIAL_BODIES> was_open{};
    inline std::size_t next_window_slot = 0;

    struct SimulationCommandQueue {
    private:
        std::queue<SimulationCommand> queue;
        std::mutex mtx;

    public:
        // By value: temporaries/std::move'd commands are moved twice (cheap), lvalues copied once
        void push(SimulationCommand cmd) {
            std::lock_guard lock(mtx);
            queue.push(std::move(cmd));
        }

        std::optional<SimulationCommand> try_pop() {
            std::lock_guard lock(mtx);

            if (queue.empty()) return std::nullopt;

            const auto cmd = queue.front();
            queue.pop();

            return cmd;
        }
    };

    inline SimulationCommandQueue simulation_command_queue;

    struct StepProgress {
        bool busy = false;
        step_t completed = 0;
        step_t total = 0;
    };

    inline std::mutex step_progress_mutex;
    inline StepProgress step_progress;

    inline StepProgress get_step_progress() {
        std::lock_guard lock(step_progress_mutex);
        return step_progress;
    }

    inline void set_step_progress(const StepProgress& progress) {
        std::lock_guard lock(step_progress_mutex);
        step_progress = progress;
    }

    inline bool request_steps(const step_t count) {
        std::lock_guard lock(step_progress_mutex);

        // Defensive: Current callers already reject 0, but request_steps is the queue's public entry point
        // ReSharper disable once CppDFAConstantConditions
        if (count == 0 || step_progress.busy || config::timer.is_running()) {
            return false;
        }

        step_progress = {
            .busy      = true,
            .completed = 0,
            .total     = count
        };

        simulation_command_queue.push(StepN{.n = count});
        return true;
    }
}

namespace ui_config {
    inline Vec2 view_offset_2d_m;
    inline Vec3 view_offset_3d_m;
    inline bool panning_2d = false;
    inline bool panning_3d = false;
    inline Vector2 drag_last_mouse_px{}; // pointer position of the previous drag frame (touch-safe delta)
    inline std::size_t followed_celestial_body_index = config::NO_BODY_SELECTED;

    inline float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    inline float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    inline float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units

    inline bool view_3d = false; // default; toggle with keybind / UI
    inline bool view_debug = false; // default; toggle with keybind
    inline bool legend_collapsed = false; // default; toggle with keybind / UI
    inline bool legend_collapse_requested = false; // helper

    inline std::array<std::chrono::steady_clock::time_point, NUM_CELESTIAL_BODIES> last_copied{};

    inline bool draw_moon_orbits = true; // default; toggle with keybind / UI
    inline bool draw_orbits = true; // default; toggle with keybind / UI

    // Inspector orbital elements: true = body + its current satellites (-> system barycenter), false = body alone
    inline bool orbit_use_system_barycenter = true;

    inline std::size_t trail_segments_reduced_2d = 0;
    inline std::size_t trail_segments_submitted_2d = 0;

    inline bool show_checkpoints = false;
    inline bool show_diagnostics = false;

    enum class BodySizeMode {
        schematic, // fixed celestial body size (schematic, radius_2d)
        physical, // fixed celestial body size (physical, physical_radius_m)
        physical_with_markers // adaptive; std::max(schematic, physical)
    };

    inline auto body_size_mode = BodySizeMode::physical_with_markers;

    inline float marker_scale = 1.0f;

    inline Vec2 get_2d_offset(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index >= snap->bodies.size() || !snap->bodies[
                followed_celestial_body_index])
            return view_offset_2d_m;
        const auto& pos = snap->bodies[followed_celestial_body_index]->relative_position;
        return {pos.x + view_offset_2d_m.x, pos.y + view_offset_2d_m.y};
    }

    inline Vec3 get_3d_offset(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index >= snap->bodies.size() || !snap->bodies[
                followed_celestial_body_index])
            return {};

        return snap->bodies[followed_celestial_body_index]->relative_position;
    }

    constexpr float CAMERA_FOVY = 45.0f;
    constexpr float MIN_CAM_DISTANCE = 1.0e-6f;
    constexpr float MAX_CAM_DISTANCE = 200.0f;

    // 3d schematic/marker radii pixels
    constexpr float PLANET_MIN_RADIUS_PX_3D = 4.0f;
    constexpr float PLANET_MAX_RADIUS_PX_3D = 10.0f;
    constexpr float MOON_MIN_RADIUS_PX_3D = 3.0f;
    constexpr float MOON_MAX_RADIUS_PX_3D = 7.5f;

    // 2d schematic/marker radii always uses radius_2d

    // keeps tiny planets clickable
    constexpr float MIN_CLICK_RADIUS_PX_2D = 8.0f;
    constexpr float MIN_CLICK_RADIUS_PX_3D = 12.0f;

    inline Model star_model{};
    inline constexpr auto STAR_BACKGROUND_PATH = "resources/8k_stars_milky_way.jpg";
    // Alternative: "resources/stars.jpg"
    inline Texture2D star_texture{};
    inline bool star_background_loaded = false;

    inline std::vector<PlanetVisual> body_textures;

    inline std::array<std::uint64_t, NUM_CELESTIAL_BODIES> seen_body_ids{};
    inline bool seen_body_ids_initialized = false;

    struct CreateBodyDraft {
        str name = "Body 1";
        double mass = 1e22; // kg
        double radius_m = 1e6; // m
        float color[3] = {
            static_cast<float>(config::CUSTOM_BODY_DEFAULT_COLOR.r) / 255.0f,
            static_cast<float>(config::CUSTOM_BODY_DEFAULT_COLOR.g) / 255.0f,
            static_cast<float>(config::CUSTOM_BODY_DEFAULT_COLOR.b) / 255.0f
        };
        std::optional<std::size_t> reference_index = 3; // Validated each frame like the inspector does
        Vec3 position{1e8, 1e8, 1e6}; // m, relative to the reference (absolute if None)
        Vec3 velocity{}; // m/s, relative to the reference

        std::optional<std::size_t> texture_id;
        // TODO: j2, equatorial_radii, pole_axes; also configurable in inspector etc.
    };

    inline bool show_create_body = false;
    inline CreateBodyDraft create_body_draft;
    inline std::uint64_t created_body_counter = 1; // For default names

    // Creating new bodies
    enum class BodyPlacementState {
        idle, armed, dragging
    };

    inline auto body_placement_state = BodyPlacementState::idle;
    inline CreateBodyDraft body_placement_backup; // Restored on cancel
    inline Vector2 placement_press_px{}; // To tell a click from a drag

    inline bool scene_context_menu_requested = false;
    inline Vec2 scene_context_menu_point_m{};
    inline std::optional<std::size_t> scene_context_menu_body;

    inline bool escape_consumed = false; // Set when a scene action used Escape this frame

    enum class DropKind {
        texture, state, unsupported
    };

    struct DroppedFile {
        str path;
        DropKind kind;
    };

    // Valid for one frame; window-specific consumers (texture rows) reset it when they take it
    inline std::optional<DroppedFile> dropped_file;

    // Adding new textures
    inline str texture_error; // Last load error, shown in the window that received the drop
    inline std::size_t texture_error_source = config::NO_BODY_SELECTED; // NO_BODY_SELECTED for creation panel
    inline constexpr auto TEXTURE_FILE_EXTENSIONS = ".png;.jpg;.jpeg;.bmp;.qoi;.gif";
    // Must match raylib's enabled loaders (config.h defaults + SUPPORT_FILEFORMAT_JPG in CMakeLists.txt)

    // Automatic camera fitting (on switching views or centering a body)
    inline std::optional<std::size_t> pending_camera_fit;

    // CSV/JSON Export, JSON import status
    struct FileStatus {
        str text;
        bool failed = false;
    };

    inline FileStatus csv_export_status, json_export_status, import_status;
}

namespace ui_helpers {
    // Inline functions

    inline bool zoom_key_down(const bool zoom_in) {
#ifdef __EMSCRIPTEN__
        return EM_ASM_INT({return Module.heldKeys[$0] ? 1 : 0; }, zoom_in ? 0 : 1) != 0;
#else
        return IsKeyDown(zoom_in ? config::KEY_ZOOM_IN : config::KEY_ZOOM_OUT);
#endif
    }

    // Pointer movement since the last drag frame
    // GetMouseDelta() breaks on touch: a new touch teleports the cursor from the previous finger position, and that jump would be applied as a drag
    [[nodiscard]] inline Vector2 consume_drag_delta() {
        const Vector2 now = GetMousePosition();
        const Vector2 delta{now.x - ui_config::drag_last_mouse_px.x, now.y - ui_config::drag_last_mouse_px.y};
        ui_config::drag_last_mouse_px = now;
        return delta;
    }

    // Orbits something that itself orbits something (ex. Sun -> planet -> moon)
    [[nodiscard]] inline bool is_moon(const ui_data::RenderSnapshot& snap, const std::size_t i) {
        return depth(snap.hierarchy, i) >= 2;
    }

    [[nodiscard]] inline double simulated_years() {
        return static_cast<double>(config::steps_simulated.load()) * runtime_config::time_step /
               config::SECONDS_PER_YEAR;
    }

    [[nodiscard]] inline double measured_simulation_speed() {
        const double seconds = config::timer.seconds();
        return seconds > 0.0 ? simulated_years() / seconds : 0.0;
    }

    [[nodiscard]] inline ImU32 to_imgui_color(const Color color) {
        return IM_COL32(color.r, color.g, color.b, color.a);
    }

    [[nodiscard]] inline std::pair<Vec2, Vec2> center_button_coordinates() {
        return {
            {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10},
            {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}
        };
    }

    [[nodiscard]] inline std::pair<Vec2, Vec2> copy_button_coordinates() {
        return {
            {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 30},
            {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 50}
        };
    }

    inline void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
        DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)),
                   Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
    }

    inline std::size_t populated_body_count(const ui_data::RenderSnapshot& snap) {
        return static_cast<std::size_t>(std::ranges::count_if(
            snap.bodies,
            [](const auto& body) { return body.has_value(); }
        ));
    }

    [[nodiscard]] inline Color to_color(const float rgb[3]) {
        const auto channel = [](const float c) {
            return static_cast<unsigned char>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
        };
        return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), 255};
    }

    inline void to_float3(const Color c, float rgb[3]) {
        rgb[0] = static_cast<float>(c.r) / 255.0f;
        rgb[1] = static_cast<float>(c.g) / 255.0f;
        rgb[2] = static_cast<float>(c.b) / 255.0f;
    }

    // Inline Structs
    struct Pinch {
        bool active = false;
        double log_scale = 0.0; // ln(distance / previous distance): + = fingers apart = zoom in
        Vector2 center{}; // finger midpoint (logical px)
        Vector2 center_delta{}; // midpoint movement since the previous frame (two-finger pan)
    };

    // Two-finger gesture from raylib's touch points (web; desktop GLFW reports no multitouch)
    inline Pinch read_pinch() {
        static bool was_active = false;
        static float previous_distance = 0.0f;
        static Vector2 previous_center{};

        if (GetTouchPointCount() < 2) {
            was_active = false;
            return {};
        }

        const auto [ax, ay] = GetTouchPosition(0);
        const auto [bx, by] = GetTouchPosition(1);
        const float distance = std::hypot(ax - bx, ay - by);
        const Vector2 center{(ax + bx) / 2.0f, (ay + by) / 2.0f};

        Pinch pinch{.active = true, .center = center};
        if (was_active && previous_distance > 0.0f && distance > 0.0f) {
            // First frame only sets the reference
            pinch.log_scale = std::log(distance / previous_distance);
            pinch.center_delta = {center.x - previous_center.x, center.y - previous_center.y};
        }

        was_active = true;
        previous_distance = distance;
        previous_center = center;
        return pinch;
    }

    // ImGui helpers

    bool imgui_double_input(ui_data::EditableDouble& edit, double current_value, std::size_t i, const str& label,
                            const str& id, int precision, const str& unit);

    bool imgui_draft_double_input(double& value, const str& label, const str& id, int precision, const str& unit,
                                  float label_width, float field_width);

    bool begin_labeled_combo(const str& label, const str& id, const str& preview, float width,
                             const std::optional<Color>& color);

    bool body_selectable(const str& name, bool selected, const std::optional<Color>& color);

    void draw_imgui_text(const str& text, Vec2 pos, float font_size, Color color);

    void draw_imgui_centered_text(const str& text, Vec2 center, float angle, float font_size, Color color);

    void draw_imgui_circle(const Vec2& pos, float radius, Color color);

    void draw_imgui_rectangle(const Vec2& a, const Vec2& b, Color color);

    void draw_imgui_outlined_text(const str& text, const Vector2& pos, float font_size, const Color& fill,
                                  const Color& outline);

    void imgui_style_setup(bool dark_mode);

    void imgui_style_cleanup();

    bool reset_icon_button(const str& id);

    enum class ButtonTone { danger, success };

    // Button with tone-colored background/hover/active states; true when clicked
    bool tinted_button(const char* label, ButtonTone tone, bool dark_mode, const ImVec2& size = {0.0f, 0.0f});

    // Confirmation popup with a red Delete and a Cancel (Escape cancels);
    // true on the frame Delete is clicked
    // Open it with ImGui::OpenPopup(popup_id) from the triggering button
    bool confirm_delete_popup(const char* popup_id, const str& question, const char* detail, bool dark_mode);

    // Windows

    void draw_debug_overlay(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void draw_legend(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_planet_info(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_toolbar(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_checkpoint_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_diagnostics_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_create_body_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_scene_context_menu(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    // An empty scene would otherwise look like a rendering bug
    void draw_empty_scene_overlay();

    // Graphs

    void open_graph_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap,
                           std::optional<std::size_t> body = std::nullopt,
                           std::optional<std::size_t> reference = std::nullopt);

    void draw_graph_windows(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void forget_graph_body(std::size_t i);

    std::optional<double> create_draft_mu(const ui_data::RenderSnapshot& snap);

    double placement_tau(const ui_data::RenderSnapshot& snap);

    std::optional<ConicSectionPath> create_ghost_conic_section_path(const ui_data::RenderSnapshot& snap);

    str full_state_csv(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void capture_pending_graph();

    // Generic helpers

    // Absolute position of the snapshot's coordinate origin (zero for None), derived from the snapshot itself
    std::optional<Vec3> snapshot_origin_absolute(const ui_data::RenderSnapshot& snap);

    // Ghost position in the snapshot's frame (what renderers draw)
    std::optional<Vec3> create_ghost_preview_position(const ui_data::RenderSnapshot& snap);

    // Sets draft.position.x/y so the ghost appears at the snapshot-frame point (x, y); z stays
    bool set_draft_xy_from_snapshot_point(const ui_data::RenderSnapshot& snap, Vec2 point);

    void cancel_placement();

    void handle_placement_input(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    std::optional<ui_data::RenderSnapshot::Body> make_create_ghost(const ui_data::RenderSnapshot& snap);

    std::array<std::optional<PointMass>, NUM_CELESTIAL_BODIES> to_point_masses(const ui_data::RenderSnapshot& snap);

    void cancel_camera_drag();

    void reset_view_offsets();

    void reset_view();

    void set_coordinate_origin(std::size_t index);

    void sync_body_identities(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    // Desktop: Writes exports/<filename> and returns that path
    // Web: starts a browser download and returns the filename
    // returns std::nullopt if writing failed
    std::optional<str> save_user_file(const str& filename, str_view contents, const char* mime_type);

    void receive_dropped_file();

    // After a restore/import: Old drafts refer to a state that no longer exists
    void reset_editor_drafts();

    // Loading

    // Decodes all textures of enabled bodies plus the star background and uploads them
    // Desktop: Decodes on decode threads while drawing a progress screen
    // Web: Synchronous (the browser shell shows its own download progress)
    // Returns false if the window was closed during loading
    bool load_resources_with_progress();

    void register_catalog_textures();

    // Renderer thread only; Same path -> same ID; std::nullopt if the file can't be decoded/uploaded
    std::optional<std::size_t> register_custom_texture_file(const str& path);

    void poll_texture_uploads();

    void discard_pending_textures();

    // IO state
#ifdef __EMSCRIPTEN__
    void receive_picked_state_file(); // Renderer thread, once per frame

    void arm_state_file_picker(bool armed); // Renderer thread, every frame
#endif

    [[nodiscard]] str state_json(const ui_data::RenderSnapshot& snap); // Schema: simulation-orbit-state v1

    // Parses and validates a simulation-orbit-state v1 file; the error string is shown to the user
    [[nodiscard]] std::variant<ui_data::ImportStateCommand, str> parse_state_json(str_view text, const str& filename);

    // Parses, validates, sets import_status
    void import_state_text(str_view text, const str& filename);

    // Desktop drop / Emscripten MEMFS drop
    void import_state_file(const str& path);

    // 3d helpers

    // Synchronous decode + upload
    void load_star_background();

    // Renderer thread only (GL): Star sphere from an already decoded image; the caller keeps ownership of `image`
    void upload_star_background(const Image& image);

    void unload_star_background();

    // Shared 2d & 3d (needed for ui/ui.cpp)
    std::optional<std::size_t> pick_body_at_mouse_2d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    bool mouse_on_create_ghost_2d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void draw_2d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    std::optional<std::size_t> pick_body_at_mouse_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap,
                                                     const Camera3D& cam);

    bool mouse_on_create_ghost_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const Camera3D& cam);

    void fit_camera_to_planetary_system_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap,
                                           std::size_t center_index);

    Camera3D make_camera_3d();

    std::optional<Vec3> cursor_on_plane_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap,
                                           const Camera3D& cam, double plane_z);

    void draw_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void UpdateDrawFrame();
}
