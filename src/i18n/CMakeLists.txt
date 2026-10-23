# Open SoC Explorer: user interface languages (English, Simplified Chinese).
create_ripes_lib(i18n LINK_TO_RIPES_LIB LINK_LIBS Qt6::Widgets)

# The translation catalogue is compiled with lrelease and embedded as a
# resource (":/i18n/open-soc-explorer_zh_CN.qm"), so that it also works in
# the WebAssembly build. Update it from the sources with the
# "osx_update_translations" target (see docs/DEVELOPMENT_GUIDE.md).
set(OSX_TS_FILES
    ${CMAKE_SOURCE_DIR}/translations/open-soc-explorer_zh_CN.ts)

find_package(Qt6 COMPONENTS LinguistTools QUIET)
if(Qt6LinguistTools_FOUND)
    set(OSX_LRELEASE_EXTRA)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
        # Also include Qt's own translations (standard buttons and dialogs).
        set(QT_NO_MISSING_CATALOG_LANGUAGE_WARNING ON)
        set(OSX_LRELEASE_EXTRA MERGE_QT_TRANSLATIONS
            QT_TRANSLATION_CATALOGS qtbase)
    endif()
    qt_add_lrelease(
        TS_FILES ${OSX_TS_FILES}
        LRELEASE_TARGET osx_lrelease
        OPTIONS -silent
        QM_FILES_OUTPUT_VARIABLE OSX_QM_FILES
        ${OSX_LRELEASE_EXTRA})
    qt_add_resources(i18n_lib "osx_translations"
        PREFIX "/i18n"
        BASE ${CMAKE_CURRENT_BINARY_DIR}
        FILES ${OSX_QM_FILES})

    # lupdate over all of src/ (Open SoC Explorer and upstream Ripes, .ui
    # files included); obsolete entries are dropped.
    add_custom_target(osx_update_translations
        COMMAND $<TARGET_FILE:Qt6::lupdate> -silent -recursive
                ${CMAKE_SOURCE_DIR}/src -extensions cpp,h,ui
                -locations none -no-obsolete -ts ${OSX_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Updating translations/open-soc-explorer_zh_CN.ts"
        VERBATIM)
else()
    # Cross builds (WebAssembly): the target Qt has no LinguistTools package,
    # but the host Qt (QT_HOST_PATH) has lrelease.
    find_program(OSX_LRELEASE NAMES lrelease lrelease-qt6
        HINTS ${QT_HOST_PATH}/bin ${QT_HOST_PATH}/libexec
              ${QT_HOST_PATH}/lib/qt6/bin
        NO_CMAKE_FIND_ROOT_PATH)
    if(OSX_LRELEASE)
        set(OSX_QM_FILE ${CMAKE_CURRENT_BINARY_DIR}/open-soc-explorer_zh_CN.qm)
        add_custom_command(OUTPUT ${OSX_QM_FILE}
            COMMAND ${OSX_LRELEASE} -silent ${OSX_TS_FILES} -qm ${OSX_QM_FILE}
            DEPENDS ${OSX_TS_FILES}
            COMMENT "Compiling the Chinese translation with ${OSX_LRELEASE}"
            VERBATIM)
        qt_add_resources(i18n_lib "osx_translations"
            PREFIX "/i18n"
            BASE ${CMAKE_CURRENT_BINARY_DIR}
            FILES ${OSX_QM_FILE})
        message(STATUS "Chinese translation compiled with ${OSX_LRELEASE}")
    else()
        message(WARNING "Qt LinguistTools not found: building without the "
                        "Chinese translation.")
    endif()
endif()
