#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <imgui.h>
#include <raylib.h>

#include "ui.hpp"

bool ui_helpers::imgui_input(ui_data::EditableDouble& edit, const double current_value, std::size_t i, const str& label, const str& id, int precision, const str& unit) {
    // Accepted values:
    // - All finite negative & positive values (& 0)

    // Rejected values:
    // - Non-finite values (-inf, +inf, nan)

    // Update continuously unless the user is editing or has an unsubmitted draft
    if (!edit.editing && !edit.dirty) {
        edit.val = current_value;
        // A "pending" state is not necessary here, because
        // - either the UI jumps back to the old value (if the input was invalid)
        // - or the UI only shows the old value for at most ~1 frame, until a publish arrives and the UI updates
    }

    const bool was_editing = edit.editing;

    ImGui::PushID(static_cast<int>(i));
    ImGui::PushID(id.c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", label.c_str());
    ImGui::SameLine();

    constexpr float field_width = 150.0f;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();

    ImGui::GetWindowDrawList()->AddRectFilled(
        start,
        ImVec2(start.x + field_width, start.y + height),
        ImGui::GetColorU32(
            was_editing ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg
        ),
        ImGui::GetStyle().FrameRounding
    );

    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImGui::SetNextItemWidth(field_width - height);

    if (ImGui::InputDouble(
        "##value",
        &edit.val,
        0.0,
        0.0,
        std::format("%.{}e", precision).c_str()
    )) {
        edit.dirty = true;
    }

    // Read immediately after InputDouble
    const bool active = ImGui::IsItemActive();
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool input_had_focus = active || (was_editing && deactivated);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    const bool enter = input_had_focus && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = input_had_focus && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enter or ✓ to apply\n"
            "Escape while editing to discard"
        );
    }

    ImGui::SameLine(0.0f, 0.0f);

    bool apply = false;

    // Keep it present on the frame when clicking it deactivates the input
    if (active || was_editing) {
        ImGui::BeginDisabled(!edit.dirty);

        ImGui::InvisibleButton("##apply", ImVec2(height, height));

        // Commit on mouse-down, before hiding the button on a later frame.
        apply = ImGui::IsItemActivated();

        const ImVec2 p = ImGui::GetItemRectMin();
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddLine(
            {p.x + height * 0.25f, p.y + height * 0.50f},
            {p.x + height * 0.43f, p.y + height * 0.68f},
            color, 2.0f
        );
        dl->AddLine(
            {p.x + height * 0.43f, p.y + height * 0.68f},
            {p.x + height * 0.77f, p.y + height * 0.30f},
            color, 2.0f
        );

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Apply this field (Enter)");
        }

        ImGui::EndDisabled();
    } else {
        ImGui::Dummy(ImVec2(height, height));
    }

    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextUnformatted(unit.c_str());

    bool committed = edit.dirty && (enter || apply);

    if (escape) {
        edit.val = current_value;
        edit.dirty = false;
        committed = false;
    }

    if (committed) {
        edit.dirty = false;
    }

    edit.editing = active && !committed && !escape;

    ImGui::PopID();
    ImGui::PopID();

    return committed;
}

bool ui_helpers::begin_labeled_combo(const str &label, const str &id, const str &preview, const float width, const std::optional<Color>& color) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();

    // Capture the combo position and parent window before BeginCombo
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const float height = ImGui::GetFrameHeight();
    const float icon_width = ImGui::CalcTextSize("   ").x;

    const str displayed_preview = color ? "   " + preview : preview;

    ImGui::SetNextItemWidth(width);

    const bool open = ImGui::BeginCombo(
        id.c_str(),
        displayed_preview.c_str(),
        ImGuiComboFlags_HeightRegular
    );

    if (color) {
        draw_list->AddCircleFilled(
            {
                start.x + padding.x + icon_width * 0.5f,
                start.y + height * 0.5f
            },
            4.0f,
            ui_helpers::to_imgui_color(*color)
        );
    }

    return open;
}

bool ui_helpers::body_selectable(const str& name, const bool selected, const std::optional<Color>& color) {
    const float height = ImGui::GetTextLineHeightWithSpacing();

    const bool clicked = ImGui::Selectable(
        "##body", selected,
        ImGuiSelectableFlags_None,
        ImVec2(0.0f, height)
    );

    const ImVec2 start = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const float center_y = (start.y + end.y) * 0.5f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (color) {
        draw_list->AddCircleFilled(
            ImVec2(start.x + 8.0f, center_y),
            4.0f,
            to_imgui_color(*color)
        );
    }

    draw_list->AddText(
        ImVec2(
            start.x + 20.0f,
            center_y - ImGui::GetTextLineHeight() * 0.5f
        ),
        ImGui::GetColorU32(ImGuiCol_Text),
        name.c_str()
    );

    return clicked;
}

void ui_helpers::draw_imgui_text(const std::string& text, const Vec2 pos, const float font_size, const Color color) {
    ImGui::GetBackgroundDrawList()->AddText(
        config::imgui_ui_font,
        font_size,
        ImVec2(static_cast<float>(std::round(pos.x)), static_cast<float>(std::round(pos.y))),
        to_imgui_color(color),
        text.c_str()
    );
}

void ui_helpers::draw_imgui_centered_text(const str& text, const Vec2 center, const float angle, const float font_size, const Color color) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    const ImVec2 size = config::imgui_ui_font->CalcTextSizeA(
        font_size, FLT_MAX, 0.0f, text.c_str()
    );

    const ImVec2 target(
        static_cast<float>(center.x),
        static_cast<float>(center.y)
    );

    // For rotated text, draw at a safe location first to avoid clipping glyphs before their vertices are rotated.
    const ImVec2 pos = angle == 0.0f
        ? ImVec2(target.x - size.x / 2, target.y - size.y / 2)
        : ImVec2(
            ImGui::GetMainViewport()->Pos.x + 20.0f,
            ImGui::GetMainViewport()->Pos.y + 20.0f
        );

    const int start = draw_list->VtxBuffer.Size;

    draw_list->AddText(
        config::imgui_ui_font,
        font_size,
        pos,
        to_imgui_color(color),
        text.c_str()
    );

    if (angle == 0.0f) return;

    const float radians = angle * DEG2RAD;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    const ImVec2 origin(
        pos.x + size.x / 2,
        pos.y + size.y / 2
    );

    // Rotate only the vertices we just created
    for (int i = start; i < draw_list->VtxBuffer.Size; i++) {
        ImVec2& p = draw_list->VtxBuffer[i].pos;

        const float x = p.x - origin.x;
        const float y = p.y - origin.y;

        p.x = target.x + x * c - y * s;
        p.y = target.y + x * s + y * c;
    }
}

void ui_helpers::draw_imgui_circle(const Vec2& pos, const float radius, const Color color) {
    ImGui::GetBackgroundDrawList()->AddCircleFilled(
        ImVec2(
            static_cast<float>(pos.x),
            static_cast<float>(pos.y)
        ),
        radius,
        to_imgui_color(color)
    );
}

void ui_helpers::draw_imgui_rectangle(const Vec2& a, const Vec2& b, const Color color) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(
            static_cast<float>(std::min(a.x, b.x)),
            static_cast<float>(std::min(a.y, b.y))
        ),
        ImVec2(
            static_cast<float>(std::max(a.x, b.x)),
            static_cast<float>(std::max(a.y, b.y))
        ),
        to_imgui_color(color)
    );
}

void ui_helpers::draw_imgui_outlined_text(const str& text, const Vector2& pos, const float font_size, const Color& fill, const Color& outline) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;

            draw_list->AddText(
                config::imgui_ui_font,
                font_size,
                ImVec2(pos.x + static_cast<float>(dx), pos.y + static_cast<float>(dy)),
                to_imgui_color(outline),
                text.c_str()
            );
        }
    }

    draw_list->AddText(
        config::imgui_ui_font,
        font_size,
        ImVec2(pos.x, pos.y),
        to_imgui_color(fill),
        text.c_str()
    );
}

void ui_helpers::imgui_style_setup(const bool dark_mode) {
    const ImU32 text_color = dark_mode ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255);
    const ImU32 background_color = dark_mode ? IM_COL32(20, 20, 20, 255) : IM_COL32(245, 245, 245, 255);
    const ImU32 button_color = dark_mode ? IM_COL32(40, 40, 40, 255) : IM_COL32(220, 220, 220, 255);
    const ImU32 button_hovered_color = dark_mode ? IM_COL32(60, 60, 60, 255) : IM_COL32(200, 200, 200, 255);
    const ImU32 button_active_color = dark_mode ? IM_COL32(80, 80, 80, 255) : IM_COL32(180, 180, 180, 255);
    const ImU32 seperator_color = dark_mode ? IM_COL32(90, 90, 90, 255) : IM_COL32(160, 160, 160, 255);

    // Text & Background
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);

    // Title bar
    ImGui::PushStyleColor(ImGuiCol_TitleBg, button_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, background_color);

    // Buttons
    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);

    // Background of many elements (ex. text input)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, button_color);

    // Combo preview hover/active states
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, button_active_color);

    // Combo dropdown and tooltip background
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background_color);

    // Dropdown entries: selected, hovered, pressed
    ImGui::PushStyleColor(ImGuiCol_Header, button_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, button_active_color);

    // Popup/tooltip outline
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        dark_mode
            ? IM_COL32(90, 90, 90, 255)
            : IM_COL32(160, 160, 160, 255)
    );

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    // Scrollbar (ex. in Combos)
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, background_color);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, button_color);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, button_active_color);

    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);

    // Seperator
    ImGui::PushStyleColor(ImGuiCol_Separator, seperator_color);

    // Slider
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, button_active_color);

    // Checkmarks
    ImGui::PushStyleColor(ImGuiCol_CheckMark, text_color);

    // Font
    ImGui::PushFont(config::imgui_ui_font);
}

void ui_helpers::imgui_style_cleanup() {
    ImGui::PopFont();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(24);
}

bool ui_helpers::reset_icon_button(const str& id) {
    const float size = ImGui::GetFrameHeight();
    const bool clicked = ImGui::Button(id.c_str(), ImVec2(size, size));

    const ImVec2 start = ImGui::GetItemRectMin();
    const ImVec2 center(start.x + size * 0.5f, start.y + size * 0.5f);
    const float radius = size * 0.24f;
    const float arrow_size = size * 0.12f;
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->PathArcTo(center, radius, 0.15f * PI, 1.5f * PI, 20);
    dl->PathStroke(color, ImDrawFlags_None, 1.5f);

    // Arc ends at the top; its tangent points right
    const ImVec2 end(center.x, center.y - radius);

    dl->AddTriangleFilled(
        ImVec2(end.x + arrow_size * 1.5f, end.y),
        ImVec2(end.x, end.y - arrow_size),
        ImVec2(end.x, end.y + arrow_size),
        color
    );
    return clicked;
}
