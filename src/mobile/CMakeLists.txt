# Open SoC Explorer: the phone user interface (UiMode::Mobile, see
# src/platform/uimode.h). It uses the Ripes library (ProcessorHandler, the
# Explorer widgets and the desktop main window for switching), hence the link
# back to it; CMake resolves the cycle between these static libraries.
create_ripes_lib(mobile LINK_TO_RIPES_LIB LINK_LIBS Qt6::Widgets)
target_link_libraries(mobile_lib PUBLIC ${RIPES_LIB})
