cmake_minimum_required(VERSION 3.16)
project(AquaRegulator VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# 查找 redis-plus-plus 和 hiredis
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(REDIS_PLUS_PLUS REQUIRED redis++)

if (WIN32)
    set(HPSOCKET_ROOT "${CMAKE_SOURCE_DIR}/../3rdparty/win32/hp-socket-6.0.7-lib-win")
elseif(UNIX)
    set(HPSOCKET_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/linux/hp-socket-6.0.7-lib-linux")
endif()

add_subdirectory(3rdparty/libmodbus)

message(STATUS "HPSOCKET_ROOT: ${HPSOCKET_ROOT}")

include_directories(${HPSOCKET_ROOT}/include)
link_directories(${HPSOCKET_ROOT}/lib/hpsocket/x64)

add_executable(AquaRegS 
    main.cxx
    services/transport/video_manager.cxx
    core/logger.cxx
    core/configuration.cxx
    infrastructure/database/mariadb_client.cxx
    infrastructure/database/telemetry_repository.cxx
    monitoring/health_monitor.cxx
)

target_include_directories(AquaRegS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HPSOCKET_ROOT}/include
    ${HIREDIS_INCLUDE_DIRS}
    ${REDIS_PLUS_PLUS_INCLUDE_DIRS}
)

target_link_libraries(AquaRegS PRIVATE
    hpsocket
    modbus
    mariadbclient
    Threads::Threads
    ${HIREDIS_LIBRARIES}
    ${REDIS_PLUS_PLUS_LIBRARIES}
)

if (WIN32)
    add_custom_command(TARGET AquaRegS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${HPSOCKET_ROOT}/lib/hpsocket/x64/HPSocket.dll"
    $<TARGET_FILE_DIR:AquaRegS>
    )
elseif(UNIX)
    add_custom_command(
        TARGET AquaRegS POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/post_build.sh"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running post build script"
    )
endif()
