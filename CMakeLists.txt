cmake_minimum_required(VERSION 3.16)

project(DBMS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        constants/table_def.h
        constants/thread_perf_def.h
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
        tests/test_entry.h
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
        # ── SQL Parser 层（纯解析，不碰文件系统）──
        utils/sql_parser/sql_tokenizer.h
        utils/sql_parser/sql_tokenizer.cpp
        utils/sql_parser/sql_parser.h
        utils/sql_parser/database_parser.cpp
        utils/sql_parser/table_parser.cpp
        utils/sql_parser/tuple_parser.cpp
        # ── SQL Dispatcher（桥接 parser → service）──
        controller/sql_dispatcher.h
        controller/sql_dispatcher.cpp
        controller/nest_query.h
        controller/nest_query.cpp
        # ── Display 层（Qt 界面，不碰 repo）──
        display/editor_panel.h
        display/editor_panel.cpp
        display/result_panel.h
        display/result_panel.cpp
        display/structure_panel.h
        display/structure_panel.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(DBMS
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET BDMS APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(DBMS SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(DBMS
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_include_directories(DBMS PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/sql_parser
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/logic
        ${CMAKE_CURRENT_SOURCE_DIR}/display
)

target_link_libraries(DBMS PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.DBMS)
endif()
set_target_properties(DBMS PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    # WIN32_EXECUTABLE TRUE  # temporarily disabled to see console output
)

include(GNUInstallDirs)
install(TARGETS DBMS
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(DBMS)
endif()
