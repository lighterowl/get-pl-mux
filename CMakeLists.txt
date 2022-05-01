cmake_minimum_required(VERSION 3.12)
project(get-pl-mux C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_C_EXTENSIONS FALSE)

if((${CMAKE_C_COMPILER_ID} STREQUAL "GNU") OR
        (${CMAKE_C_COMPILER_ID} MATCHES "Clang"))
    add_compile_options(-Wall -Wextra)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)

include_directories(${GLIB_INCLUDE_DIRS})
link_libraries(${GLIB_LIBRARIES})

find_package(LibXml2 REQUIRED)
find_package(CURL REQUIRED)

add_library(deser OBJECT deser.c muxdata.c)

add_library(parser OBJECT parser.c)
target_link_libraries(parser ${LIBXML2_LIBRARIES})
target_compile_definitions(parser PUBLIC ${LIBXML2_DEFINITIONS})
target_include_directories(parser PUBLIC ${LIBXML2_INCLUDE_DIRS})

add_executable(test_deser test/deser.c)
target_link_libraries(test_deser deser)

add_executable(test_html2xml test/html2xml.c)
target_link_libraries(test_html2xml deser parser)

add_executable(get-pl-mux main.c arguments.c fetch.c)
target_compile_options(get-pl-mux PRIVATE ${GSTREAMER_CFLAGS_OTHER})
target_link_libraries(get-pl-mux parser deser ${GSTREAMER_LIBRARIES}
    ${CURL_LIBRARIES} ${GIO_LIBRARIES})
target_include_directories(get-pl-mux PRIVATE ${GSTREAMER_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
