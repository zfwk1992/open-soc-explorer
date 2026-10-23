cmake_minimum_required(VERSION 3.9)

find_package(Qt6 COMPONENTS Test REQUIRED)

# Point to bundled tests within source directory
set(RISCV32_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv-tests)
set(RISCV64_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv-tests-64)
set(RISCV32_C_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv-tests-c)
set(RISCV64_C_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv-tests-c-64)
add_definitions(-DRISCV32_TEST_DIR="${RISCV32_TEST_DIR}")
add_definitions(-DRISCV64_TEST_DIR="${RISCV64_TEST_DIR}")
add_definitions(-DRISCV32_C_TEST_DIR="${RISCV32_C_TEST_DIR}")
add_definitions(-DRISCV64_C_TEST_DIR="${RISCV64_C_TEST_DIR}")

macro(create_qtest name)
    add_executable(${name} ${name}.cpp programloader.h)
    add_test(${name} ${name})
    target_include_directories (${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} Qt6::Core Qt6::Widgets Qt6::Test)
    target_link_libraries(${name} ripes_lib)
endmacro()

create_qtest(tst_riscv)
create_qtest(tst_assembler)
create_qtest(tst_expreval)
create_qtest(tst_cosimulate)
create_qtest(tst_reverse)
create_qtest(tst_stall)

# Open SoC Explorer tests
add_definitions(-DOSX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
create_qtest(tst_trace)
create_qtest(tst_missions)
create_qtest(tst_explorergui)
create_qtest(tst_mobilegui)
# The GUI test constructs the main window, which needs the resources that are
# otherwise only compiled into the application executable.
qt6_add_resources(GUI_TEST_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/icons/icons.qrc
    ${CMAKE_SOURCE_DIR}/examples/examples.qrc
    ${CMAKE_SOURCE_DIR}/src/processors/layouts.qrc
    ${CMAKE_SOURCE_DIR}/resources/fonts/fonts.qrc)
target_sources(tst_explorergui PRIVATE ${GUI_TEST_RESOURCES})
target_sources(tst_mobilegui PRIVATE ${GUI_TEST_RESOURCES})
