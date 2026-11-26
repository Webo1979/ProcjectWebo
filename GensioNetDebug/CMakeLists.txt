cmake_minimum_required(VERSION 3.20)
project(GensioNetDebug VERSION 1.0 LANGUAGES CXX)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(GENSIO_ROOT "" CACHE PATH "Prefix of an installed gensio")
find_path(GENSIO_INCLUDE_DIR NAMES gensio/gensio.h HINTS "${GENSIO_ROOT}" PATH_SUFFIXES include REQUIRED)
find_library(GENSIO_LIBRARY NAMES gensio HINTS "${GENSIO_ROOT}" PATH_SUFFIXES lib lib64 REQUIRED)
find_library(GENSIO_OS_LIBRARY NAMES gensioosh HINTS "${GENSIO_ROOT}" PATH_SUFFIXES lib lib64 REQUIRED)

add_executable(GensioNetDebug WIN32 main.cpp)
target_compile_features(GensioNetDebug PRIVATE cxx_std_17)
target_include_directories(GensioNetDebug PRIVATE "${GENSIO_INCLUDE_DIR}")
target_link_libraries(GensioNetDebug PRIVATE Qt${QT_VERSION_MAJOR}::Widgets "${GENSIO_LIBRARY}" "${GENSIO_OS_LIBRARY}")
if(MSVC)
    target_compile_options(GensioNetDebug PRIVATE /utf-8)
endif()
