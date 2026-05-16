add_library(opmemu STATIC
    opm.c
    opmemu.cpp
)

target_include_directories(opmemu PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# C++ standard for compatibility
set_target_properties(opmemu PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
