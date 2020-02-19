cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

find_program(CLANG_FORMAT_EXE NAMES "clang-format" DOC "Path to clang-format executable")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

file(GLOB_RECURSE SHADER_SOURCES ${PROJECT_SOURCE_DIR}/src/*.glsl)

if (APPLE)
    add_executable(AreaLightShadows MACOSX_BUNDLE main.cpp ${SHADER_SOURCES} ${ASSET_SOURCES})
    set(MACOSX_BUNDLE_BUNDLE_NAME "AreaLightShadows") 
    set_source_files_properties(${SHADER_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shader)
    set_source_files_properties(${ASSET_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
else()
    add_executable(AreaLightShadows main.cpp) 
endif()

target_link_libraries(AreaLightShadows dwSampleFramework)

if (NOT APPLE)
    add_custom_command(TARGET AreaLightShadows POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:AreaLightShadows>/shader)
endif()

if(CLANG_FORMAT_EXE)
    add_custom_target(AreaLightShadows-clang-format COMMAND ${CLANG_FORMAT_EXE} -i -style=file main.cpp)
endif()

set_property(TARGET AreaLightShadows PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")