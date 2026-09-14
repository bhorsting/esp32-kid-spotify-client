cmake_minimum_required(VERSION 2.8.12)

project(cspot)

# Configurable options
set(CSPOT_EXTERNAL_BELL "" CACHE STRING "External bell library target name, optional")

# CMake options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_compile_definitions(NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
	add_definitions(/wd4068 /wd4244 /wd4018 /wd4101 /wd4102 /wd4142 /wd4996)
endif()

# Main library sources
file(GLOB SOURCES "src/*.cpp" "src/*.c")

# Use externally specified bell library or the submodule
if(CSPOT_EXTERNAL_BELL)
    list(APPEND EXTRA_LIBS ${CSPOT_EXTERNAL_BELL})
else()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bell)
    list(APPEND EXTRA_LIBS bell)
endif()

# Use pre-generated nanopb sources (avoids Python/protobuf version issues at build time)
file(GLOB PROTO_SRCS "protobuf/generated/*.pb.c")

add_library(cspot STATIC ${SOURCES} ${PROTO_SRCS})
target_compile_options(cspot PRIVATE -std=gnu++2a)
# PUBLIC to propagate includes from bell to cspot dependents
target_compile_definitions(bell PUBLIC PB_ENABLE_MALLOC)
target_compile_definitions(bell PUBLIC PB_FIELD_32BIT)
target_link_libraries(cspot PUBLIC ${EXTRA_LIBS})
target_include_directories(cspot PUBLIC "include" ${NANOPB_INCLUDE_DIRS} ${EXTRA_INCLUDES})
