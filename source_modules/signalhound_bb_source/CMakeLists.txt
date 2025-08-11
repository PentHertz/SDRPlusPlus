cmake_minimum_required(VERSION 3.13)
project(signalhound_bb_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # --- Windows Configuration ---
    find_path(BBAPI_INCLUDE_DIR 
              NAMES bb_api.h 
              HINTS "C:/Program Files/Signal Hound/Spike"
              DOC "Path to the Signal Hound bb_api.h header file")

    find_library(BBAPI_LIBRARY 
                 NAMES bb_api 
                 HINTS "C:/Program Files/Signal Hound/Spike"
                 DOC "Path to the Signal Hound bb_api.lib import library")

    if (BBAPI_INCLUDE_DIR AND BBAPI_LIBRARY)
        message(STATUS "Found Signal Hound BB API: ${BBAPI_LIBRARY}")
        # Add the include directory for bb_api.h
        target_include_directories(signalhound_bb_source PUBLIC ${BBAPI_INCLUDE_DIR})
        # Link against the bb_api.lib import library
        target_link_libraries(signalhound_bb_source PRIVATE ${BBAPI_LIBRARY})
    else()
        message(FATAL_ERROR "Signal Hound BB API not found. Please install the Spike software from Signal Hound.")
    endif()

elseif (ANDROID)
    # --- Android Configuration ---
    message(WARNING "The signalhound_bb_source module is not supported on Android and will not be built.")

else ()
    # --- Linux and macOS Configuration ---
    # Find the header file (e.g., /usr/local/include/bb_api.h)
    find_path(BBAPI_INCLUDE_DIR 
              NAMES bb_api.h
              PATHS /usr/include /usr/local/include
              DOC "Path to the Signal Hound bb_api.h header file")

    # Find the library. The NAME 'bb_api' will automatically find 'libbb_api.so'
    # and correctly resolve the symlinks you listed.
    find_library(BBAPI_LIBRARY 
                 NAMES bb_api
                 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
                 DOC "Path to the Signal Hound libbb_api.so shared library")

    if(BBAPI_INCLUDE_DIR AND BBAPI_LIBRARY)
        message(STATUS "Found Signal Hound BB API: ${BBAPI_LIBRARY}")
        # Add the include directory for bb_api.h
        target_include_directories(signalhound_bb_source PRIVATE ${BBAPI_INCLUDE_DIR})
        # Link against the libbb_api.so shared library
        target_link_libraries(signalhound_bb_source PRIVATE ${BBAPI_LIBRARY})
    else()
        message(FATAL_ERROR "Signal Hound BB API not found. Please install the Spike software and ensure bb_api.h and libbb_api.so are in standard system paths (e.g., /usr/local/include and /usr/local/lib).")
    endif()

endif ()
