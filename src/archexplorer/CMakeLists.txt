create_ripes_lib(archexplorer LINK_TO_RIPES_LIB)

# The architecture descriptions (resources/arch/arch.qrc, ":/arch/*.json") are
# compiled into this library instead of the application executable, so that
# both Ripes and the unit tests (which only link ripes_lib) can enumerate them.
# archmodel.cpp calls Q_INIT_RESOURCE(arch), which forces the linker to keep
# the generated resource object when linking this static library.
# NOTE: do not additionally add arch.qrc to the top-level qt_add_executable()
# call; that would define qInitResources_arch twice.
qt6_add_resources(ARCH_QRC_SRC ${CMAKE_SOURCE_DIR}/resources/arch/arch.qrc)
target_sources(archexplorer_lib PRIVATE ${ARCH_QRC_SRC})
target_link_libraries(archexplorer_lib PUBLIC Qt6::Widgets)

# archexplorer_lib uses symbols defined in ripes_lib (Ripes::localized() from
# i18n.cpp, ThemeManager, showHelp). ripes_lib already links this library, so
# declare the reverse dependency as well: CMake repeats both static libraries
# on the link line for such a cycle, which the single-pass GNU linker needs.
target_link_libraries(archexplorer_lib PUBLIC ${RIPES_LIB})
