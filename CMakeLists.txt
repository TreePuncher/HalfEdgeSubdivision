
cmake_minimum_required(VERSION 3.23)

include(FetchContent)

project(flexApplication  LANGUAGES CXX VERSION 0.0.1)

FetchContent_Declare(
  flex
  GIT_REPOSITORY "https://github.com/TreePuncher/gamekit.git"
  GIT_TAG        "editor"
)

FetchContent_MakeAvailable(flex)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file (GLOB CPP_FILES src/*.cpp)
file (GLOB HPP_FILES includes/*.hpp)

add_executable(
	TestApp
	${CPP_FILES}
)

target_include_directories(
	TestApp
	PUBLIC
	${PROJECT_SOURCE_DIR}/includes)

target_compile_options(TestApp PUBLIC /UTF-8 /bigobj)

set_property(TARGET TestApp PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(TestApp PRIVATE flex flex_optional)

Flex_CopyBinaries(TestApp)
Flex_CopyAssets(TestApp)
