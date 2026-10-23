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
elseif(QT_HOST_PATH)
    # Cross builds (WebAssembly): the target Qt may come without the
    # translation tools, which run on the build machine anyway; use the host
    # Qt's lrelease (and lconvert to add Qt's own qtbase catalogue).
    find_program(OSX_HOST_LRELEASE NAMES lrelease lrelease-qt6
        PATHS ${QT_HOST_PATH}/bin ${QT_HOST_PATH}/libexec
              ${QT_HOST_PATH}/lib/qt6/bin
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_program(OSX_HOST_LCONVERT NAMES lconvert lconvert-qt6
        PATHS ${QT_HOST_PATH}/bin ${QT_HOST_PATH}/libexec
              ${QT_HOST_PATH}/lib/qt6/bin
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(OSX_HOST_LRELEASE)
        set(OSX_QM ${CMAKE_CURRENT_BINARY_DIR}/open-soc-explorer_zh_CN.qm)
        set(OSX_QT_QM ${QT_HOST_PATH}/translations/qtbase_zh_CN.qm)
        if(OSX_HOST_LCONVERT AND EXISTS ${OSX_QT_QM})
            set(OSX_MERGE_QT_QM
                COMMAND ${OSX_HOST_LCONVERT} -o ${OSX_QM} ${OSX_QM}
                        ${OSX_QT_QM})
        endif()
        add_custom_command(
            OUTPUT ${OSX_QM}
            COMMAND ${OSX_HOST_LRELEASE} -silent ${OSX_TS_FILES} -qm ${OSX_QM}
            ${OSX_MERGE_QT_QM}
            DEPENDS ${OSX_TS_FILES}
            COMMENT "Compiling the Chinese translation (host lrelease)"
            VERBATIM)
        qt_add_resources(i18n_lib "osx_translations"
            PREFIX "/i18n"
            BASE ${CMAKE_CURRENT_BINARY_DIR}
            FILES ${OSX_QM})
        message(STATUS "Translation compiled with ${OSX_HOST_LRELEASE}")
    else()
        message(WARNING "lrelease not found in ${QT_HOST_PATH}: building "
                        "without the Chinese translation.")
    endif()
else()
    message(WARNING "Qt LinguistTools not found: building without the "
                    "Chinese translation.")
endif()
