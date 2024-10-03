
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

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	file(TO_NATIVE_PATH ${PROJECT_SOURCE_DIR}/assets/shaders/CBT/CBT.hlsl SOURCEPATH)
	file(TO_NATIVE_PATH ${PROJECT_BINARY_DIR}/assets/shaders/CBT/CBT.hlsl TARGETPATH)

	add_custom_command(TARGET TestApp POST_BUILD
		COMMAND mklink /j ${TARGETPATH} ${SOURCEPATH} || echo ""
		COMMAND_EXPAND_LISTS)
else()
	add_custom_command(
		TARGET TestApp POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets/shaders/CBT $<TARGET_FILE_DIR:TestApp>/assets/shaders/CBT
		COMMAND_EXPAND_LISTS)
endif()

Flex_CopyBinaries(TestApp)
Flex_CopyAssets(TestApp)
