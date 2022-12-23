cmake_minimum_required(VERSION 3.17)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR} CACHE INTERNAL "")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

if(MSVC)
	add_definitions(/MP)
endif()

project(apilearning)

file(GLOB SOURCES "source/*.cpp" "source/*.hpp" "source/*.h")
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${PROJECT_NAME}")

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_INSTALL OFF)
add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)

add_subdirectory(deps/glm EXCLUDE_FROM_ALL)

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
	
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE 
	glfw
	glm
	${Vulkan_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${Vulkan_INCLUDE_DIRS}
)

set_property(TARGET ${RPOJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})