function(patch_source file before after label)
    file(READ "${file}" source)
    string(FIND "${source}" "${after}" done)
    if(NOT done EQUAL -1)
    return()
    endif()
    string(FIND "${source}" "${before}" pos)
    if(pos EQUAL -1)
    message(FATAL_ERROR "${label}: unexpected source; review the patch")
    endif()
    string(REPLACE "${before}" "${after}" source "${source}")
    file(WRITE "${file}" "${source}")
    message(STATUS "Applied ${label}")
endfunction()

function(patch_web_hidpi)
    if(NOT RAYLIB_VERSION STREQUAL "6.0")
    message(FATAL_ERROR "Review the web HiDPI patch for this raylib version")
    endif()
    set(web "${raylib_SOURCE_DIR}/src/platforms/rcore_web.c")

    # 1) Enable Emscripten-GLFW HiDPI (canvas framebuffer = CSS size * devicePixelRatio)
    patch_source("${web}" [=[    //if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI)) glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    //else glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);]=] [=[    if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI)) glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    else glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);]=] "raylib web HiDPI hint")

    # 2) GLFW passes the logical (CSS) size: viewport = framebuffer, screen = logical, draw calls scaled
    patch_source("${web}" [=[    // Reset viewport and projection matrix for new size
    SetupViewport(width, height);

    CORE.Window.currentFbo.width = width;
    CORE.Window.currentFbo.height = height;
    CORE.Window.resizedLastFrame = true;

    if (IsWindowFullscreen()) return;

    // Set current screen size
    if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI))
    {
        Vector2 windowScaleDPI = GetWindowScaleDPI();

        CORE.Window.screen.width = (unsigned int)(width/windowScaleDPI.x);
        CORE.Window.screen.height = (unsigned int)(height/windowScaleDPI.y);
    }
    else
    {
        CORE.Window.screen.width = width;
        CORE.Window.screen.height = height;
    }]=] [=[    int fbWidth = width;
    int fbHeight = height;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    // Reset viewport and projection matrix for new size
    SetupViewport(fbWidth, fbHeight);

    CORE.Window.currentFbo.width = fbWidth;
    CORE.Window.currentFbo.height = fbHeight;
    CORE.Window.resizedLastFrame = true;
    CORE.Window.screenScale = MatrixScale((float)fbWidth/(float)width, (float)fbHeight/(float)height, 1.0f);

    if (IsWindowFullscreen()) return;

    CORE.Window.screen.width = width;
    CORE.Window.screen.height = height;]=] "raylib web HiDPI window size")

    # 3) Browser resize: Let GLFW size the canvas (HiDPI-aware), then apply the result
    patch_source("${web}" [=[    emscripten_set_canvas_element_size(platform.canvasId, width, height);

    SetupViewport(width, height); // Reset viewport and projection matrix for new size

    CORE.Window.currentFbo.width = width;
    CORE.Window.currentFbo.height = height;
    CORE.Window.resizedLastFrame = true;

    if (IsWindowFullscreen()) return 1;

    // Set current screen size
    CORE.Window.screen.width = width;
    CORE.Window.screen.height = height;

    glfwSetWindowSize(platform.handle, CORE.Window.screen.width, CORE.Window.screen.height);]=] [=[    if (IsWindowFullscreen()) return 1;

    glfwSetWindowSize(platform.handle, width, height);
    WindowSizeCallback(platform.handle, width, height); // GLFW skips its callback if the logical size is unchanged (e.g. DPR change)]=] "raylib web HiDPI resize")

    # 4) rlImGui: Use the real DPR on the web as well
    patch_source("${rlimgui_SOURCE_DIR}/rlImGui.cpp" [=[#if defined(__EMSCRIPTEN__)
    return Vector2{ 1,1 };
#else]=] [=[#if 0 // patched: web HiDPI handled by raylib patch
    return Vector2{ 1,1 };
#else]=] "rlImGui web HiDPI scale")
endfunction()
