if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds are not allowed. Please use a separate build directory.")
endif()

cmake_minimum_required(VERSION 3.13)

# 设置项目名称和版本号
project(WebServer VERSION 1.0)

# 指定 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 设置为Debug版
# set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -Wall -g -ggdb")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -O0 -Wall -g -ggdb")

# 设置为Release版
# set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -O3")

# 添加相关头文件目录
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# 添加子目录以构建库和可执行文件
add_subdirectory(code)
# add_subdirectory(test)


# 遵循以下顺序
# cmake_minimum_required：通常是在CMakeLists.txt文件的开头使用，用于指定所需的CMake最低版本。
# project：设置项目名称和版本信息。通常紧随cmake_minimum_required之后。
# set：用于设置变量，可以在任何位置使用，根据需要放置。
# include_directories：用于包含头文件的目录路径。建议在添加源文件之前设置。
# add_subdirectory：用于添加子目录，包含其他模块或库的构建。通常在设置变量和添加源文件之后。
# add_executable和add_library：用于定义可执行文件和库的目标。通常在设置变量和添加源文件之后。
# target_link_libraries：用于指定目标所依赖的库。通常在定义目标之后，以确保正确的链接。