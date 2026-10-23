# Open SoC Explorer: trace protocol, Ripes trace adapter, replay, instruction
# explanations and learning missions.
create_ripes_lib(explorer LINK_TO_RIPES_LIB LINK_LIBS io_lib)

# Built-in learning missions (loaded via Q_INIT_RESOURCE(missions)).
qt6_add_resources(MISSIONS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/missions/missions.qrc)
target_sources(explorer_lib PRIVATE ${MISSIONS_SRC})

# Example gem5 trace (":/osx-examples/gem5/loop_sum_o3_l1l2.jsonl", File →
# Load Example and Explorer tool bar): the recorded gem5 O3CPU run in
# tools/gem5/testdata, converted with tools/gem5/gem5_to_osx.py at build time
# so that the repository only holds gem5's own (compressed) output. Without a
# Python 3 interpreter the example is left out and its actions are disabled.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(OSX_GEM5_DIR ${CMAKE_SOURCE_DIR}/tools/gem5)
    set(OSX_GEM5_RUN ${OSX_GEM5_DIR}/testdata/loop_sum_o3_l1l2)
    set(OSX_GEM5_EXAMPLE
        ${CMAKE_CURRENT_BINARY_DIR}/osx-examples/gem5/loop_sum_o3_l1l2.jsonl)
    add_custom_command(
        OUTPUT ${OSX_GEM5_EXAMPLE}
        COMMAND ${Python3_EXECUTABLE} ${OSX_GEM5_DIR}/gem5_to_osx.py
                --trace ${OSX_GEM5_RUN}/trace.out.gz --m5out ${OSX_GEM5_RUN}
                --binary ${OSX_GEM5_DIR}/testdata/loop_sum.elf
                --gem5-version v26.0.0.0 --created-utc 2026-10-22T12:00:00Z
                -o ${OSX_GEM5_EXAMPLE}
        DEPENDS ${OSX_GEM5_DIR}/gem5_to_osx.py ${OSX_GEM5_RUN}/trace.out.gz
                ${OSX_GEM5_RUN}/config.json.gz ${OSX_GEM5_RUN}/stats.txt.gz
                ${OSX_GEM5_DIR}/testdata/loop_sum.elf
        COMMENT "Converting the example gem5 trace"
        VERBATIM)
    qt_add_resources(explorer_lib "osx_gem5_example"
        PREFIX "/"
        BASE ${CMAKE_CURRENT_BINARY_DIR}
        FILES ${OSX_GEM5_EXAMPLE})
else()
    message(WARNING "Python 3 not found: building without the example gem5 "
                    "trace.")
endif()
