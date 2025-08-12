cmake_minimum_required(VERSION 3.13)
project(kcsdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Windows - Lib path for KC SDR API
    target_link_directories(kcsdr_source PRIVATE "C:/kcsdr/lib/x64/")
    target_include_directories(kcsdr_source PRIVATE "C:/kcsdr/include/")
    target_link_libraries(kcsdr_source PRIVATE kcsdr)
else (MSVC)
    # Linux/Unix - Lib path for KC SDR API
    target_link_directories(kcsdr_source PRIVATE "/usr/lib/")
    target_include_directories(kcsdr_source PRIVATE "/usr/include/")
    
    # Link against KC SDR library and pthread for Linux
    target_link_libraries(kcsdr_source PRIVATE kcsdr pthread)
    
    # Add compiler flags to help with symbol resolution
    target_compile_definitions(kcsdr_source PRIVATE _GNU_SOURCE)
    set_target_properties(kcsdr_source PROPERTIES
        LINK_FLAGS "-Wl,--no-undefined"
    )
endif ()

# Optional: Add version info and definitions
add_definitions(-DKC_SDR_VERSION="1.0.1")

# Optional: Set C++ standard if needed
set_property(TARGET kcsdr_source PROPERTY CXX_STANDARD 17)
