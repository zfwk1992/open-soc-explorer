# Open SoC Explorer: platform facts (user interface mode: desktop or phone)
# and the fonts of the WebAssembly build.
create_ripes_lib(platform LINK_TO_RIPES_LIB LINK_LIBS Qt6::Core)

# Qt for WebAssembly only has DejaVu fonts: embed a Chinese font there
# (resources/fonts/NotoSansSC, made by tools/fonts/subset_cjk.py).
if(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    set(OSX_EMBED_CJK_FONT_DEFAULT ON)
else()
    set(OSX_EMBED_CJK_FONT_DEFAULT OFF)
endif()
option(OSX_EMBED_CJK_FONT "Embed the Chinese font (WebAssembly)"
       ${OSX_EMBED_CJK_FONT_DEFAULT})
if(OSX_EMBED_CJK_FONT)
    qt6_add_resources(WEBFONTS_SRC
        ${CMAKE_SOURCE_DIR}/resources/fonts/webfonts.qrc)
    target_sources(platform_lib PRIVATE ${WEBFONTS_SRC})
    target_compile_definitions(platform_lib PRIVATE OSX_EMBED_CJK_FONT)
    target_link_libraries(platform_lib PUBLIC Qt6::Gui)
endif()
