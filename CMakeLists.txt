cmake_minimum_required(VERSION 3.30)
project(slim_guestbook_api)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
        include/ring_buffer.h)

target_include_directories(slim_guestbook_api
        PUBLIC
        include
)

target_link_libraries(slim_guestbook_api PRIVATE Drogon::Drogon PRIVATE JsonCpp::JsonCpp)