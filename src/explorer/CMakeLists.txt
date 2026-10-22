# Open SoC Explorer: trace protocol, Ripes trace adapter, replay, instruction
# explanations and learning missions.
create_ripes_lib(explorer LINK_TO_RIPES_LIB LINK_LIBS io_lib)

# Built-in learning missions (loaded via Q_INIT_RESOURCE(missions)).
qt6_add_resources(MISSIONS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/missions/missions.qrc)
target_sources(explorer_lib PRIVATE ${MISSIONS_SRC})
