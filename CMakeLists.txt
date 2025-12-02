cmake_minimum_required(VERSION 3.16)

project(SaturnControl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets LinguistTools)

# IMPORTANTE: AUTORCC debe estar en ON para procesar las imágenes
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON) 

# Archivos de traducción
set(TS_FILES
    translations/saturn_es.ts
)

# Archivos fuente (Añadimos resources.qrc al final)
set(SOURCES
    main.cpp
    mainwindow.cpp
    backend.cpp
    resources.qrc
)

set(HEADERS
    mainwindow.h
    backend.h
    protocol.h
)

add_executable(SaturnControl ${SOURCES} ${HEADERS})

qt_add_translations(SaturnControl TS_FILES ${TS_FILES})

target_link_libraries(SaturnControl PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)
