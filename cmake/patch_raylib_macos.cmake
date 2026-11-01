function(patch_raylib_macos)
    if(NOT RAYLIB_VERSION STREQUAL "6.0")
        message(FATAL_ERROR
                "Review the macOS framebuffer patch for this raylib version"
        )
    endif()

    set(backend
            "${raylib_SOURCE_DIR}/src/platforms/rcore_desktop_glfw.c"
    )
    file(READ "${backend}" source)

    set(anchor
            "    CORE.Window.shouldClose = glfwWindowShouldClose(platform.handle);"
    )
    set(before "    else glfwPollEvents();      // Poll input events: keyboard/mouse/window events (callbacks) -> Update keys state\n\n${anchor}")

    set(fix [=[#if defined(__APPLE__)
    // Reconcile cached dimensions after native macOS window transitions.
    int framebuffer_width = 0;
    int framebuffer_height = 0;

    glfwGetFramebufferSize(
        platform.handle,
        &framebuffer_width,
        &framebuffer_height
    );

    if ((framebuffer_width > 0) && (framebuffer_height > 0) &&
        ((framebuffer_width != CORE.Window.render.width) ||
         (framebuffer_height != CORE.Window.render.height)))
    {
        FramebufferSizeCallback(
            platform.handle,
            framebuffer_width,
            framebuffer_height
        );
    }
#endif

]=])
    string(REPLACE "${anchor}" "${fix}${anchor}" after "${before}")

    string(FIND "${source}" "${after}" already_patched)
    if(NOT already_patched EQUAL -1)
        return()
    endif()

    string(FIND "${source}" "${before}" patch_position)
    if(patch_position EQUAL -1)
        message(FATAL_ERROR
                "raylib macOS framebuffer patch: unexpected source; review the patch"
        )
    endif()

    string(REPLACE "${before}" "${after}" source "${source}")
    file(WRITE "${backend}" "${source}")

    message(STATUS "Applied raylib macOS framebuffer fix")
endfunction()
