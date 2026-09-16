# Older CM0 BSP releases omit the graphics development packages.
set(YOUTUBE_GPU_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.cache/gpu-sdk" CACHE PATH
    "Optional EGL/GLES/GBM SDK overlay for cross compilation")
if(CMAKE_CROSSCOMPILING AND EXISTS "${YOUTUBE_GPU_SDK_ROOT}/usr/include/EGL/egl.h")
    foreach(_gpu_api IN ITEMS EGL GLES GBM)
        set(CM0_${_gpu_api}_INCLUDE_DIR "${YOUTUBE_GPU_SDK_ROOT}/usr/include" CACHE PATH
            "CM0 ${_gpu_api} development headers")
    endforeach()
    set(YOUTUBE_GPU_SDK_LIB_DIR "${YOUTUBE_GPU_SDK_ROOT}/usr/lib/aarch64-linux-gnu")
    find_library(CM0_EGL_LIBRARY NAMES EGL libEGL.so.1
        PATHS "${YOUTUBE_GPU_SDK_LIB_DIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(CM0_GLES_LIBRARY NAMES GLESv2 libGLESv2.so.2
        PATHS "${YOUTUBE_GPU_SDK_LIB_DIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(CM0_GBM_LIBRARY NAMES gbm libgbm.so.1
        PATHS "${YOUTUBE_GPU_SDK_LIB_DIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
