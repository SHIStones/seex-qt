cmake_minimum_required(VERSION 3.21)

project(SeExQt VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)
qt_standard_project_setup()



qt_add_executable(seex
    src/main.cpp

    src/core/Extractor.h
    src/core/Extractor.cpp
    src/core/AppState.h
    src/core/AppState.cpp
    src/core/Config.h
    src/core/Config.cpp
    src/core/Util.h
    src/core/Util.cpp
    src/core/Metadata.h
    src/core/Metadata.cpp

    src/altium/BinaryWriter.h
    src/altium/BinaryWriter.cpp
    src/altium/Cfb.h
    src/altium/Cfb.cpp
    src/altium/SchLib.h
    src/altium/SchLib.cpp
    src/altium/PcbLib.h
    src/altium/PcbLib.cpp
    src/altium/Footprint.h
    src/altium/Footprint.cpp
    src/altium/LibPkg.h
    src/altium/LibPkg.cpp
    src/altium/PcbLib.h
    src/altium/PcbLib.cpp
    src/altium/Footprint.h
    src/altium/Footprint.cpp
    src/altium/LibPkg.h
    src/altium/LibPkg.cpp

    src/net/LcedaClient.h
    src/net/LcedaClient.cpp

    src/export/Exporter.h
    src/export/Exporter.cpp

    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/Components.h
    src/ui/Components.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ImportPage.h
    src/ui/ImportPage.cpp
    src/ui/HistoryPage.h
    src/ui/HistoryPage.cpp
    src/ui/ExportPage.h
    src/ui/ExportPage.cpp
    src/ui/PreviewPage.h
    src/ui/PreviewPage.cpp
)

# The PcbLib library header and the .LibPkg document keys are copied verbatim
# from the Rust build; they live in the Qt resource bundle so the generated
# files stay byte-identical without a 28 KB string literal.
qt_add_resources(seex "seexdata"
    PREFIX "/seex"
    BASE "src/altium"
    FILES
        src/altium/pcblib_library_data_template.txt
        src/altium/libpkg_document_keys.txt
)

target_include_directories(seex PRIVATE src)

target_link_libraries(seex PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

set_target_properties(seex PROPERTIES
    WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
    OUTPUT_NAME "SeEx"
)

if(MSVC)
    target_compile_options(seex PRIVATE /utf-8 /W3)
endif()
