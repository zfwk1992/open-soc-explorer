cmake_minimum_required(VERSION 3.13)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(status "QT_HOST_PATH = ${QT_HOST_PATH}")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "CCache enabled")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

project(Ripes
        LANGUAGES CXX
        DESCRIPTION "A graphical processor simulator and assembly editor for the RISC-V ISA"
        HOMEPAGE_URL "https://github.com/mortbopet/Ripes"
)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    include(GNUInstallDirs) #Must exist after declaring a project
endif()

# Error flags on everything but MSVC
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "-Wextra -Wall \
        -Werror=switch -Werror=return-type \
        -Werror=unreachable-code")
endif()

if(MSVC)
    add_definitions(/bigobj) # Allow big object
elseif(MINGW)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj") # Allow big object
endif()

######################################################################
## GUI setup
######################################################################

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(RIPES_WITH_QPROCESS ON CACHE BOOL "Build with QProcess-related utilities")
# Add as a C++ definition
if(RIPES_WITH_QPROCESS)
    add_definitions(-DRIPES_WITH_QPROCESS)
endif()

# Find required Qt packages
find_package(Qt6 6.8 COMPONENTS Core Widgets Svg Charts REQUIRED)

# Finding Qt includes
include_directories(${Qt6Widgets_INCLUDE_DIRS})
include_directories(${Qt6Core_INCLUDE_DIRS})
include_directories(${Qt6Charts_INCLUDE_DIRS})

######################################################################
## Resources setup
######################################################################
qt6_add_resources(ICONS_SRC ${CMAKE_SOURCE_DIR}/resources/icons/icons.qrc)
qt6_add_resources(EXAMPLES_SRC ${CMAKE_SOURCE_DIR}/examples/examples.qrc)
qt6_add_resources(LAYOUTS_SRC ${CMAKE_SOURCE_DIR}/src/processors/layouts.qrc)
qt6_add_resources(FONTS_SRC ${CMAKE_SOURCE_DIR}/resources/fonts/fonts.qrc)

######################################################################
## Library setup
######################################################################

option(RIPES_BUILD_VERILATOR_PROCESSORS "Build verilator processors" OFF)
if(RIPES_BUILD_VERILATOR_PROCESSORS)
    if(NOT DEFINED ENV{VERILATOR_ROOT})
        message(FATAL_ERROR "'VERILATOR_ROOT' must be set when building Verilator-based processors!")
    endif()
    message(STATUS "Building with Verilator processors")
    include_directories(SYSTEM $ENV{VERILATOR_ROOT}/include)
    set(verilator_DIR $ENV{VERILATOR_ROOT})
    find_package(verilator)

    # Create library containing Verilator runtime files, which verilator processors can link against
    file(GLOB VERILATOR_SOURCES $ENV{VERILATOR_ROOT}/include/verilated.cpp)
    file(GLOB VERILATOR_HEADERS $ENV{VERILATOR_ROOT}/include/verilated.h)
    set(VERILATOR_LIB verilator_lib)
    add_library(${VERILATOR_LIB} ${VERILATOR_SOURCES} ${VERILATOR_HEADERS})
endif()

# Platform specific pre-target setup(s)
if(${APPLE})
    set(SYSTEM_FLAGS MACOSX_BUNDLE)
elseif(${WIN32})
    set(SYSTEM_FLAGS WIN32)
endif()

add_subdirectory(external)

# Fix the name of the ripes library.
set(RIPES_LIB ripes_lib)
set(ISA_LIB isa_lib)
add_subdirectory(src)

option(RIPES_BUILD_TESTS "Build Ripes tests" OFF)
if(RIPES_BUILD_TESTS)
    # Lets ctest find the tests registered with add_test() in test/.
    enable_testing()
    add_subdirectory(test)
endif()

set(APP_NAME Ripes)
qt_add_executable(${APP_NAME} ${SYSTEM_FLAGS} ${ICONS_SRC} ${EXAMPLES_SRC} ${LAYOUTS_SRC} ${FONTS_SRC} main.cpp)

# Link Qt libraries
target_link_libraries(${APP_NAME} PUBLIC Qt6::Core Qt6::Widgets)
# Link Ripes library
target_link_libraries(${APP_NAME} PUBLIC ${RIPES_LIB})

######################################################################
## Translations (UI language support, see src/i18n.h)
######################################################################
# Every resources/i18n/<module>_<lang>.ts is compiled with lrelease and
# embedded into the executable under ":/i18n/<module>_<lang>.qm".
# To regenerate a .ts from the sources it covers, run tools/i18n/update_ts.py;
# the module -> source mapping lives in tools/i18n/modules.json.
find_package(Qt6 COMPONENTS LinguistTools QUIET)
file(GLOB RIPES_TS_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/i18n/*.ts)
if(RIPES_TS_FILES AND Qt6LinguistTools_FOUND)
    qt6_add_lrelease(${APP_NAME}
        TS_FILES ${RIPES_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE RIPES_QM_FILES)
    qt6_add_resources(${APP_NAME} "ripes_i18n"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${RIPES_QM_FILES})
    message(STATUS "Translations: ${RIPES_TS_FILES}")
elseif(RIPES_TS_FILES)
    message(WARNING "Qt6 LinguistTools not found; UI translations will not be built")
endif()

######################################################################
## In-app user guide (Help > User Guide, src/helpdialog.h)
######################################################################
# Every Markdown chapter and image of docs/guide is embedded under
# ":/guide/<path relative to docs/guide>", e.g. ":/guide/zh/03-gui-tour.md" and
# ":/guide/images/03-gui-tour-main.zh.png". The file list is collected with a
# CONFIGURE_DEPENDS glob, so new chapters/images are picked up on the next
# build without editing any list (there is no hand-written guide.qrc).
# Note: BIG_RESOURCES is deliberately not used. With MinGW the project
# compiles with -Wa,-mbig-obj, and rcc's object-file patching (pass 2) does
# not understand the big-obj COFF format, producing a corrupt object ("bad
# string table size" at link time). Keep screenshots compressed (PNG).
file(GLOB_RECURSE RIPES_GUIDE_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/docs/guide/*.md
    ${CMAKE_SOURCE_DIR}/docs/guide/images/*.png
    ${CMAKE_SOURCE_DIR}/docs/guide/images/*.jpg
    ${CMAKE_SOURCE_DIR}/docs/guide/images/*.svg
    ${CMAKE_SOURCE_DIR}/docs/guide/images/*.gif)
if(RIPES_GUIDE_FILES)
    qt6_add_resources(${APP_NAME} "ripes_guide"
        PREFIX "/guide"
        BASE "${CMAKE_SOURCE_DIR}/docs/guide"
        FILES ${RIPES_GUIDE_FILES})
    list(LENGTH RIPES_GUIDE_FILES RIPES_GUIDE_FILE_COUNT)
    message(STATUS "User guide: ${RIPES_GUIDE_FILE_COUNT} files embedded under :/guide")
else()
    message(STATUS "User guide: docs/guide not found; Help > User Guide will look for it next to the executable")
endif()

######################################################################
## Screenshot tool for the user guide (tools/gui_shots, optional)
######################################################################
# Offscreen tool that drives the real main window and grabs the screenshots
# referenced by docs/guide (see tools/gui_shots/README.md). Off by default and
# not a test. Because build.ps1 does not forward cmake options, the default
# can also be switched on with the environment variable
# RIPES_BUILD_GUI_SHOTS=ON at configure time.
if(DEFINED ENV{RIPES_BUILD_GUI_SHOTS})
    set(_ripes_gui_shots_default $ENV{RIPES_BUILD_GUI_SHOTS})
else()
    set(_ripes_gui_shots_default OFF)
endif()
option(RIPES_BUILD_GUI_SHOTS "Build the tools/gui_shots screenshot tool" ${_ripes_gui_shots_default})
if(RIPES_BUILD_GUI_SHOTS)
    qt_add_executable(gui_shots ${ICONS_SRC} ${EXAMPLES_SRC} ${LAYOUTS_SRC} ${FONTS_SRC}
        ${CMAKE_SOURCE_DIR}/tools/gui_shots/gui_shots.cpp)
    target_link_libraries(gui_shots PRIVATE Qt6::Core Qt6::Widgets ${RIPES_LIB})
    if(RIPES_QM_FILES)
        # The .qm files are produced by the Ripes target's lrelease step.
        add_dependencies(gui_shots ${APP_NAME}_lrelease)
        qt6_add_resources(gui_shots "gui_shots_i18n"
            PREFIX "/i18n"
            BASE "${CMAKE_CURRENT_BINARY_DIR}"
            FILES ${RIPES_QM_FILES})
    endif()
    if(RIPES_GUIDE_FILES)
        qt6_add_resources(gui_shots "gui_shots_guide"
            PREFIX "/guide"
            BASE "${CMAKE_SOURCE_DIR}/docs/guide"
            FILES ${RIPES_GUIDE_FILES})
    endif()
    message(STATUS "gui_shots: screenshot tool enabled (tools/gui_shots)")
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    # https://doc.qt.io/qt-6/wasm.html#asyncify
    target_link_options(${RIPES_LIB} PUBLIC -sASYNCIFY -Os)
    target_link_options(${APP_NAME} PUBLIC -sASYNCIFY -Os)
endif()


if(UNIX AND NOT APPLE) #Define the LINUX variable before testing it
    set(LINUX TRUE)
endif()

if(${LINUX})
    install(TARGETS ${APP_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
elseif(${APPLE})
    install(TARGETS ${APP_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE  DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/appdir/usr/
    DESTINATION ${CMAKE_INSTALL_PREFIX}
)
