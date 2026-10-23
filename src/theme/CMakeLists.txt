# Open SoC Explorer: GUI theme (palette, style sheet and painting helpers).
create_ripes_lib(theme LINK_TO_RIPES_LIB LINK_LIBS Qt6::Widgets)

# Original drawings used by the style sheet (loaded via Q_INIT_RESOURCE(theme)).
qt6_add_resources(THEME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/resources/theme.qrc)
target_sources(theme_lib PRIVATE ${THEME_SRC})
