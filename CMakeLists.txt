cmake_minimum_required(VERSION 3.30)
project(slim_guestbook_api)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
enable_testing()

include(FetchContent)

add_compile_definitions(JSONCPP_USING_STD_STRING_VIEW=1)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(JSONCPP_WITH_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Fetch Jsoncpp
FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG "1.9.8"  # or choose a stable version/tag
)
FetchContent_MakeAvailable(jsoncpp)

if (TARGET jsoncpp_static AND NOT TARGET JsonCpp::JsonCpp)
    add_library(JsonCpp::JsonCpp INTERFACE IMPORTED)
    set_target_properties(JsonCpp::JsonCpp PROPERTIES INTERFACE_LINK_LIBRARIES "jsoncpp_static")
    set(JSONCPP_LIBRARIES jsoncpp_static)
elseif (TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    add_library(JsonCpp::JsonCpp INTERFACE IMPORTED)
    set_target_properties(JsonCpp::JsonCpp PROPERTIES INTERFACE_LINK_LIBRARIES "jsoncpp_lib")
    set(JSONCPP_LIBRARIES jsoncpp_lib)
endif ()

set(JSONCPP_INCLUDE_DIRS ${jsoncpp_SOURCE_DIR}/include)

# Fetch Drogon
FetchContent_Declare(
        drogon
        GIT_REPOSITORY https://github.com/drogonframework/drogon.git
        GIT_TAG "v1.9.13"  # or specify a stable tag/commit hash
)
FetchContent_MakeAvailable(drogon)

if(TARGET drogon AND NOT TARGET Drogon::Drogon)
    add_library(Drogon::Drogon ALIAS drogon)
endif()

# Copy config.json to the output directory
configure_file(${CMAKE_SOURCE_DIR}/config.json
        ${CMAKE_BINARY_DIR}/config.json
        COPYONLY)

add_executable(slim_guestbook_api main.cpp
        include/ring_buffer.h
        ring_buffer.cpp
        include/rate_limiter.h
        rate_limiter.cpp)

target_include_directories(slim_guestbook_api
        PUBLIC
        include
)

target_link_libraries(slim_guestbook_api PRIVATE Drogon::Drogon PRIVATE JsonCpp::JsonCpp)

add_executable(ring_buffer_cli
        ring_buffer_cli.cpp
        include/ring_buffer.h
        ring_buffer.cpp)

target_include_directories(ring_buffer_cli
        PRIVATE
        include
)

target_link_libraries(ring_buffer_cli PRIVATE Drogon::Drogon)

add_executable(ring_buffer_tests
        tests/ring_buffer_tests.cpp
        tests/test_framework.h
        include/ring_buffer.h
        ring_buffer.cpp)

target_include_directories(ring_buffer_tests
        PRIVATE
        include
        tests
)

target_link_libraries(ring_buffer_tests PRIVATE Drogon::Drogon)

add_test(NAME ring_buffer_tests COMMAND ring_buffer_tests)

add_executable(rate_limiter_tests
        tests/rate_limiter_tests.cpp
        tests/test_framework.h
        include/rate_limiter.h
        rate_limiter.cpp)

target_include_directories(rate_limiter_tests
        PRIVATE
        include
        tests
)

add_test(NAME rate_limiter_tests COMMAND rate_limiter_tests)

add_executable(get_page_handler_tests
        tests/get_page_handler_tests.cpp
        tests/test_framework.h
        include/ring_buffer.h
        ring_buffer.cpp
        main.cpp
        include/rate_limiter.h
        rate_limiter.cpp)

target_compile_definitions(get_page_handler_tests PRIVATE SLIM_GUESTBOOK_API_DISABLE_MAIN)

target_include_directories(get_page_handler_tests
        PRIVATE
        include
        tests
)

target_link_libraries(get_page_handler_tests PRIVATE Drogon::Drogon PRIVATE JsonCpp::JsonCpp)

add_test(NAME get_page_handler_tests COMMAND get_page_handler_tests)
