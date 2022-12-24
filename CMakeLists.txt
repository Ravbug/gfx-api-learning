cmake_minimum_required(VERSION 3.17)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR} CACHE INTERNAL "")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

if(MSVC)
	add_definitions(/MP)
endif()

project(apilearning)

file(GLOB_RECURSE SOURCES "source/*.cpp" "source/*.hpp" "source/*.h" "source/*.mm")
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${PROJECT_NAME}")

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_INSTALL OFF)
add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)

add_subdirectory(deps/glm EXCLUDE_FROM_ALL)

if(APPLE)
add_definitions("-x objective-c++")
endif()

find_package(Vulkan)
if (Vulkan_FOUND)
	macro(vk_compile infile)
		get_filename_component(name_only ${infile} NAME)
		set(outname "${CMAKE_CURRENT_BINARY_DIR}/${name_only}.spv")
		list(APPEND all_vk_shders "${outname}")
		add_custom_command(
			PRE_BUILD
			OUTPUT "${outname}"
			DEPENDS ${infile}
			COMMAND Vulkan::glslc "${infile}" -o "${outname}"
		)
	endmacro()

	file(GLOB vk_shaders "source/shaders/*.vert" "source/shaders/*.frag")
	foreach(FILE ${vk_shaders})
		vk_compile("${FILE}")
	endforeach()

	add_custom_target(${PROJECT_NAME}_VkShaders
		DEPENDS ${all_vk_shders}
	)
	add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_VkShaders)
	set(VK_LIBS ${Vulkan_LIBRARIES})
	set(VK_INCLUDE ${Vulkan_INCLUDE_DIRS})
	target_compile_definitions(${PROJECT_NAME} PRIVATE VK_AVAILABLE=1)
else()
	target_compile_definitions(${PROJECT_NAME} PRIVATE VK_AVAILABLE=0)
endif()

if(MSVC)
	add_subdirectory(deps/DirectX-Headers EXCLUDE_FROM_ALL)
	set(DX12_LIBRARIES "d3d12.lib" "DirectX-Headers" "DirectX-Guids" "dxgi.lib" "d3dcompiler.lib")
	target_compile_definitions(${PROJECT_NAME} PRIVATE DX12_AVAILABLE=1)
	target_sources(${PROJECT_NAME} PRIVATE source/shaders/dx_vs.hlsl source/shaders/dx_fs.hlsl)
	set_source_files_properties(source/shaders/dx_vs.hlsl PROPERTIES VS_SHADER_MODEL "5.1" VS_SHADER_TYPE "Vertex")
	set_source_files_properties(source/shaders/dx_fs.hlsl PROPERTIES VS_SHADER_MODEL "5.1" VS_SHADER_TYPE "Pixel")
else()
	target_compile_definitions(${PROJECT_NAME} PRIVATE DX12_AVAILABLE=0)
endif()

if(APPLE)
	target_compile_definitions(${PROJECT_NAME} PRIVATE MTL_AVAILABLE=1)
	set(MTL_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/deps/metal-cpp" "${CMAKE_CURRENT_LIST_DIR}/deps/metal-cpp-extensions")
	find_library(FOUNDATION_LIB Cocoa REQUIRED)
    find_library(METAL_LIB Metal REQUIRED)
    find_library(QZC_LIB QuartzCore REQUIRED)
    target_sources(${PROJECT_NAME} PRIVATE source/shaders/metal.metal)
else()
	target_compile_definitions(${PROJECT_NAME} PRIVATE MTL_AVAILABLE=0)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE 
	glfw
	glm
	${VK_LIBS}
	${DX12_LIBRARIES}
	${FOUNDATION_LIB}
	${METAL_LIB}
	${QZC_LIB}
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${VK_INCLUDE}
	${MTL_INCLUDE}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
	MACOSX_BUNDLE TRUE
	RESOURCE "metal.metal"
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>"
)
