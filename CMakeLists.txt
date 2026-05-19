cmake_minimum_required(VERSION 3.16)

project(DBMS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Core Test)

set(DBMS_CORE_SOURCES
        constants/table_def.h
        constants/thread_perf_def.h
        constants/cli_client_def.h
        utils/table_manu/table_manu.h
        utils/table_manu/type_utils.cpp
        utils/table_manu/schema_maint.cpp
        utils/table_manu/schema_builder.cpp
        utils/service_common/service_common.h
        utils/thread_runtime/lock_manager.h
        utils/thread_runtime/lock_manager.cpp
        utils/thread_runtime/catalog_cache.h
        utils/thread_runtime/catalog_cache.cpp
        utils/service_common/name_normalize.cpp
        utils/service_common/constraint_worker.cpp
        utils/service_common/sql_stringifier.cpp
        utils/logic/logic.h
        utils/logic/logic_types.h
        utils/logic/logic_ast.h
        utils/logic/logic_tokenizer.h
        utils/logic/logic_tokenizer.cpp
        utils/logic/logic_parser.h
        utils/logic/logic_parser.cpp
        utils/logic/simple_logic.h
        utils/logic/simple_logic.cpp
        utils/logic/set_logic.h
        utils/logic/set_logic.cpp
        utils/logic/subquery_logic.h
        utils/logic/subquery_logic.cpp
        utils/logic/logic_evaluator.h
        utils/logic/logic_evaluator.cpp
        service/service.h
        service/database_service.cpp
        service/auth_service.h
        service/auth_service.cpp
        service/table_service.cpp
        service/tuple_service.cpp
        service/table_dml_service.cpp
        repo/repo.h
        repo/database_repo.cpp
        repo/constraint_repo.cpp
        repo/index_repo.cpp
        repo/meta_repo.cpp
        repo/tab_repo.cpp
        repo/table_repo.cpp
        repo/sort_index_repo.cpp
        # ── SQL Parser 层（纯解析，不碰文件系统）──
        utils/sql_parser/sql_tokenizer.h
        utils/sql_parser/sql_tokenizer.cpp
        utils/sql_parser/sql_parser.h
        utils/sql_parser/auth_parser.cpp
        utils/sql_parser/database_parser.cpp
        utils/sql_parser/table_parser.cpp
        utils/sql_parser/tuple_parser.cpp
        controller/sql_dispatcher.h
        controller/sql_dispatcher.cpp
        controller/nest_query.h
        controller/nest_query.cpp
        client/client_session.h
        client/client_session_pool.h
        client/client_session_pool.cpp
        client/sql_result_formatter.h
        client/sql_result_formatter.cpp
        client/sql_client_engine.h
        client/sql_client_engine.cpp
)

set(DBMS_GUI_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        display/editor_panel.h
        display/editor_panel.cpp
        display/result_panel.h
        display/result_panel.cpp
        display/structure_panel.h
        display/structure_panel.cpp
        display/create_table_dialog.h
        display/create_table_dialog.cpp
        display/add_column_dialog.h
        display/add_column_dialog.cpp
        resources/resources.qrc
)

set(DBMS_TEST_SOURCES
        tests/test_entry.h
        cli/cli_app.h
        cli/cli_app.cpp
        tests/test_parser_dispatcher.cpp
        tests/test_database_service.cpp
        tests/test_logic.cpp
        tests/test_query_executor.cpp
        tests/test_table_service.cpp
        tests/test_tuple_service.cpp
        tests/test_lock_manager.cpp
        tests/test_threaded_service.cpp
        tests/test_catalog_cache.cpp
        tests/test_service_common_cache.cpp
        tests/test_table_runtime_pipeline.cpp
        tests/test_index_runtime_repair.cpp
        tests/test_client_session.cpp
        tests/test_cli_client.cpp
        tests/test_auth_client.cpp
        tests/test_gui_client_runtime.cpp
        tests/test_integration_stress.cpp
)

set(DBMS_CLI_SOURCES
        cli/main_cli.cpp
        cli/cli_app.h
        cli/cli_app.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(DBMS
        MANUAL_FINALIZATION
        ${DBMS_CORE_SOURCES}
        ${DBMS_GUI_SOURCES}
        ${DBMS_TEST_SOURCES}
    )
    qt_add_executable(DBMS_CLI
        MANUAL_FINALIZATION
        ${DBMS_CORE_SOURCES}
        ${DBMS_CLI_SOURCES}
    )
else()
    if(ANDROID)
        add_library(DBMS SHARED
            ${DBMS_CORE_SOURCES}
            ${DBMS_GUI_SOURCES}
            ${DBMS_TEST_SOURCES}
        )
    else()
        add_executable(DBMS
            ${DBMS_CORE_SOURCES}
            ${DBMS_GUI_SOURCES}
            ${DBMS_TEST_SOURCES}
        )
        add_executable(DBMS_CLI
            ${DBMS_CORE_SOURCES}
            ${DBMS_CLI_SOURCES}
        )
    endif()
endif()

target_include_directories(DBMS PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/sql_parser
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/logic
        ${CMAKE_CURRENT_SOURCE_DIR}/display
)

target_include_directories(DBMS_CLI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/sql_parser
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/logic
)

target_link_libraries(DBMS PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
target_link_libraries(DBMS_CLI PRIVATE Qt${QT_VERSION_MAJOR}::Core)

target_compile_definitions(DBMS PRIVATE
    DBMS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(DBMS_CLI PRIVATE
    DBMS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(DBMS_CLI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

if(WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    get_target_property(QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DBMS_CLI POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    "$<TARGET_FILE:DBMS_CLI>"
            COMMENT "Deploying Qt runtime dependencies for DBMS_CLI"
        )
    endif()
endif()

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.DBMS)
endif()
set_target_properties(DBMS PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
)

include(GNUInstallDirs)
install(TARGETS DBMS DBMS_CLI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(DBMS)
    qt_finalize_executable(DBMS_CLI)
endif()
