#-------------------------------------------------------------------------
# codec2 - CMake
#-------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.5)
INCLUDE(FindPkgConfig)
project(codec2)

set(CMAKE_CXX_STANDARD 14)
pkg_check_modules(LIBION libion)

# TODO add -Werror
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-error=deprecated-declarations -Wno-enum-compare -stdlib=libstdc++ \
	-I/usr/include/drm \
	-I/usr/include/c++/ \
	-I/usr/include/c++/${TARGET_SYS} \
	-I${LIBION_INCLUDE_DIRS} ")


add_definitions(-DC2PORT)

# Build debug version
# set(CMAKE_BUILD_TYPE Debug)

# Include directories
set(CODEC2_INCLUDES
#C2 includes
    core/include/
    vndk/include/
    vndk/internal/
    vndk_platform/include/)

# Include directories
set(build_include_dirs
	${CODEC2_INCLUDES})
include_directories(${build_include_dirs})

# Output path
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

#-------------------------------------------------------------------------
# libcodec2_vndk
#-------------------------------------------------------------------------

set(CODEC2_VNDK_SOURCE
#C2 vndk code
    vndk/util/C2ParamUtils.cpp
    vndk/util/C2InterfaceUtils.cpp
    vndk/util/C2InterfaceHelper.cpp
    vndk/util/C2Debug.cpp
    vndk/C2Config.cpp
    vndk/C2Buffer.cpp
    vndk/C2Store.cpp
#C2 platform vndk implementation
    vndk_platform/C2AllocatorGBM.cpp
    vndk/C2AllocatorIon.cpp)
add_library(codec2_vndk SHARED ${CODEC2_VNDK_SOURCE})
target_link_libraries(codec2_vndk gbm ion cutils)

set(CODEC2_VNDK_LIBS
    codec2_vndk)

install(TARGETS ${CODEC2_VNDK_LIBS} DESTINATION "/usr/lib/")
install(DIRECTORY ${CODEC2_INCLUDES} DESTINATION "/usr/include/")
