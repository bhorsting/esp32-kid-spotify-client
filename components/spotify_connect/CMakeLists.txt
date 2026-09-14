idf_component_register(
    SRCS "spotify_connect.cpp" "spotify_webapi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls mdns esp_wifi nvs_flash esp_event esp_netif driver esp_http_server
             esp_http_client freertos board_audio
)

set(BELL_DISABLE_FMT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_WEBSERVER ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++2a;-Wno-error=overloaded-virtual" APPEND)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../third_party/cspot/cspot"
                 ${CMAKE_CURRENT_BINARY_DIR}/cspot)

target_compile_options(cspot PRIVATE -std=gnu++2a -Wno-error=overloaded-virtual)
target_compile_options(bell PRIVATE -std=gnu++2a -Wno-error=overloaded-virtual)
target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++2a)
