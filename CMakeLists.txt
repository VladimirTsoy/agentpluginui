cmake_minimum_required(VERSION 3.5)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
add_definitions(-DUNICODE -D_UNICODE)
project(AgentPluginUI LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# remove "dll" prefix for Windows
if (NOT UNIX)
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()

file(GLOB SOURCES
    ./src/*.cpp
    ./src/*.h
    ./src/*.ui
    ./src/*.qrc
)

include_directories(${CMAKE_BINARY_DIR})
configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_BINARY_DIR}/version.h" @ONLY)

#set(Boost_LIBRARIES filesystem system)
#set(Boost_USE_STATIC_LIBS ON)
#set(Boost_USE_MULTITHREADED ON)
#set(Boost_USE_STATIC_RUNTIME OFF)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package (Threads REQUIRED)
    #${CMAKE_THREAD_LIBS_INIT})

#if (UNIX AND NOT APPLE)
    #target_link_libraries(AgentPluginUI ${CMAKE_DL_LIBS})
#endif()

find_package(Qt5 COMPONENTS Core Network REQUIRED)
message(STATUS "Qt5Core Version:  ${Qt5Core_VERSION}")
message(STATUS "Qt5Network Version:  ${Qt5Network_VERSION}")
message("ProjectDir = " ${PROJECT_BINARY_DIR})

#set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_LIBS OFF)
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_RUNTIME OFF)
set (Boost_NO_SYSTEM_PATHS FALSE)
find_package(Boost 1.72.0 REQUIRED COMPONENTS filesystem exception thread)
if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
    link_directories(${Boost_LIBRARY_DIRS})
    add_definitions("-DHAS_BOOST")
endif()

find_package(OpenSSL 1.1.0 REQUIRED COMPONENTS Crypto SSL)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIBRARIES})
endif()


find_package(AthenaAgent REQUIRED)
if (ATHENA_AGENT_FOUND)
    include_directories(${ATHENA_AGENT_INCLUDE_DIRS})
endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL 1.1.0 REQUIRED COMPONENTS Crypto SSL)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIBRARIES})
endif()


add_library(AgentPluginUI SHARED ${SOURCES})

find_package(Threads REQUIRED)
if (Threads_FOUND)
    message("link threads")
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    #target_link_libraries(AgentPluginUI pthread)
    if ("${CMAKE_C_COMPILER}" STREQUAL "/usr/bin/x86_64-w64-mingw32-gcc-posix")
        message("link mingw winpthread")
        target_link_libraries(${PROJECT_NAME} -static winpthread -dynamic)
    endif()
endif()


if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_DL_LIBS})
    target_compile_definitions(${PROJECT_NAME} PUBLIC SHARED_APP_DATA_PATH="${PROJECT_BINARY_DIR}/..")
endif()

if (Boost_FOUND)
    target_link_libraries(${PROJECT_NAME} Boost::filesystem Boost::exception Boost::thread)
endif()
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Network)

if(OPENSSL_FOUND)
    message("link openssl")
    target_link_libraries(${PROJECT_NAME} OpenSSL::SSL )
    target_link_libraries(${PROJECT_NAME} OpenSSL::Crypto)
endif()


if (NOT UNIX)
    target_compile_definitions(${PROJECT_NAME} PUBLIC SHARED_APP_DATA_PATH="C:/ProgramData")
    message("link winlibs")
    target_link_libraries(${PROJECT_NAME} ws2_32)
    target_link_libraries(${PROJECT_NAME} wsock32)
    target_link_libraries(${PROJECT_NAME} wbemuuid)
    target_link_libraries(${PROJECT_NAME} -static -static-libgcc -static-libstdc++ -dynamic)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC AGENTPLUGINUI_LIBRARY)
