if(TARGET modules)
    file(GLOB_RECURSE PBC_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")

    if(TARGET coa)
        set_property(SOURCE
            "${CMAKE_CURRENT_LIST_DIR}/src/api/pbc_http_handlers.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/core/pbc_main.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/events/pbc_player_scripts.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/helpers/pbc_scene_helpers.cpp"
            APPEND PROPERTY COMPILE_DEFINITIONS PBC_COA_CORE)
    endif()

    # Use the role API of whichever CoA bot modules are linked into this target.
    if(TARGET coa AND "mod-playerbots" IN_LIST STATIC_SCRIPT_MODULES
        AND EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Ai/Class/Coa/CoaSpecialization.h")
        set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/helpers/pbc_scene_helpers.cpp"
            APPEND PROPERTY COMPILE_DEFINITIONS PBC_COA_SQUIDBOTS)
    endif()
    if(TARGET coa AND "mod-coa-playerbots" IN_LIST STATIC_SCRIPT_MODULES
        AND EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-coa-playerbots/src/BotMgr.h")
        set_property(SOURCE
            "${CMAKE_CURRENT_LIST_DIR}/src/helpers/pbc_bot_helpers.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/helpers/pbc_scene_helpers.cpp"
            APPEND PROPERTY COMPILE_DEFINITIONS PBC_COA_PLAYERBOTS)
    endif()

    # Optional mod_weather_vibe integration
    if(NOT "${MODULE_MOD_WEATHER_VIBE}" STREQUAL "disabled" AND EXISTS "${CMAKE_SOURCE_DIR}/modules/mod_weather_vibe/src/core/mod_wv_core.h")
        target_compile_definitions(modules PRIVATE MOD_WEATHER_VIBE)
        target_include_directories(modules PRIVATE
            "${CMAKE_SOURCE_DIR}/modules/mod_weather_vibe/src/core")
        message(STATUS "[mod-pbc] mod_weather_vibe found and enabled - weather integration enabled")
    else()
        message(STATUS "[mod-pbc] mod_weather_vibe not found or disabled - weather integration disabled")
    endif()
    # Include nlohmann/json library
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/deps/nlohmann/json.hpp")
        set_property(SOURCE ${PBC_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/deps")
        message(STATUS "[mod-pbc] Using bundled nlohmann/json (namespaced as pbc_nlohmann)")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/nlohmann")
        target_include_directories(modules PRIVATE ${CMAKE_SOURCE_DIR}/deps/nlohmann)
        message(STATUS "[mod-pbc] Using AzerothCore deps nlohmann/json")
    else()
        find_package(nlohmann_json CONFIG QUIET)
        if(nlohmann_json_FOUND)
            target_link_libraries(modules PRIVATE nlohmann_json::nlohmann_json)
            message(STATUS "[mod-pbc] Using system nlohmann/json")
        else()
            message(FATAL_ERROR "[mod-pbc] nlohmann/json not found.\n"
                              "  Please place json.hpp in deps/nlohmann/json.hpp\n"
                              "  or install via: apt install nlohmann-json3-dev")
        endif()
    endif()

    # Include fmt library
    if(TARGET fmt)
        target_link_libraries(modules PRIVATE fmt)
        message(STATUS "[mod-pbc] Using AzerothCore fmt library")
    else()
        find_package(fmt CONFIG QUIET)
        if(fmt_FOUND)
            target_link_libraries(modules PRIVATE fmt::fmt)
            message(STATUS "[mod-pbc] Using system fmt library")
        else()
            message(FATAL_ERROR "[mod-pbc] fmt library not found.\n"
                              "  Ubuntu/Debian: sudo apt install libfmt-dev\n"
                              "  Or build AzerothCore with full deps")
        endif()
    endif()

    # Include cpp-httplib (header-only)
    # Bundled version (v0.43.1) in deps/yhirose/cpp-httplib/httplib.h provides HTTP server + WebSocket support.
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/deps/yhirose/cpp-httplib/httplib.h")
        set_property(SOURCE ${PBC_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/deps/yhirose/cpp-httplib")
        message(STATUS "[mod-pbc] Using bundled cpp-httplib v0.43.1 (with WebSocket support)")
    else()
        message(FATAL_ERROR "[mod-pbc] cpp-httplib not found.\n"
                          "  Please place httplib.h in deps/yhirose/cpp-httplib/httplib.h\n"
                          "  Download from: https://github.com/yhirose/cpp-httplib")
    endif()

    # Enable SSL/TLS support for HTTPS connections
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND OR OPENSSL_FOUND)
        # CoA partitions the static modules target on MSVC. Include paths from
        # OpenSSL's imported targets do not reach those partitions.
        set_property(SOURCE ${PBC_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
        set_property(SOURCE ${PBC_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS CPPHTTPLIB_OPENSSL_SUPPORT)
        target_link_libraries(modules PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "[mod-pbc] OpenSSL found - HTTPS support enabled")
    else()
        message(WARNING "[mod-pbc] OpenSSL not found - only HTTP (no HTTPS) available")
        if(WIN32)
            message(STATUS "[mod-pbc] Windows: vcpkg install openssl")
        else()
            message(STATUS "[mod-pbc] Linux: apt install libssl-dev")
        endif()
    endif()

    # Platform-specific threading and networking
    if(WIN32)
        target_link_libraries(modules PRIVATE ws2_32 crypt32)
    else()
        target_link_libraries(modules PRIVATE pthread)
    endif()
endif()
