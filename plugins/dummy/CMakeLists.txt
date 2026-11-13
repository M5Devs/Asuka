# Dummy reference plugin: implements IEngineCore and exports the factory
# entry points. Built as a shared library so the runner loads it dynamically.

add_library(asuka_dummy_engine SHARED
    DummyEngine.cpp
    dummy_plugin.cpp)

target_include_directories(asuka_dummy_engine PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(asuka_dummy_engine PRIVATE asuka_warnings)

# Hide every symbol except the explicitly exported factory entry points.
set_target_properties(asuka_dummy_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
