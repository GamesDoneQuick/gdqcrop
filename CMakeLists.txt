cmake_minimum_required(VERSION 3.2)
project(gdq-crop)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

#option(QT_DIR "" CACHE PATH "location of the Qt environment suited for your compiler and architecture")
set(LIBOBS_INCLUDE_DIR "" CACHE PATH "location of the libobs subfolder in the source code of OBS Studio")
set(LIBOBS_LIB  "" CACHE FILEPATH "location of the obs.lib file")
set(OBS_FRONTEND_LIB "" CACHE FILEPATH "location of the obs-frontend-api.lib file")

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)


set(ENABLE_PROGRAMS false)

set(gdq-crop_SOURCES
	gdq-crop.c)

set(gdq-crop_config_HEADERS)

# --- Platform-independent build settings ---
add_library(gdq-crop MODULE 
	${gdq-crop_SOURCES}
	${gdq-crop_config_HEADERS})

include_directories( 
	"${CMAKE_BINARY_DIR}/plugins/gdq-crop/config"
	"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES})

target_link_libraries(gdq-crop 
	libobs
	Qt5::Core
	Qt5::Widgets)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(WIN32)
	if(NOT DEFINED OBS_FRONTEND_LIB)
		set(OBS_FRONTEND_LIB "OBS_FRONTEND_LIB-NOTFOUND" CACHE FILEPATH "OBS frontend library")
		message(FATAL_ERROR "Could not find OBS Frontend API\'s library !")
	endif()

	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(ARCH_NAME "64bit")
		set(OBS_BUILDDIR_ARCH "build64")
	else()
		set(ARCH_NAME "32bit")
		set(OBS_BUILDDIR_ARCH "build32")
	endif()

	include_directories(
		"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/UI"
	)

	target_link_libraries(gdq-crop
		"${OBS_FRONTEND_LIB}")

	# --- Release package helper ---
	# The "release" folder has a structure similar OBS' one on Windows
	set(RELEASE_DIR "${PROJECT_SOURCE_DIR}/release")

	add_custom_command(TARGET gdq-crop POST_BUILD
		# If config is Release, package release files
		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E make_directory
			"${RELEASE_DIR}/data/obs-plugins/gdq-crop"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy_directory
			"${PROJECT_SOURCE_DIR}/data"
			"${RELEASE_DIR}/data/obs-plugins/gdq-crop")

		COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy
			"$<TARGET_FILE:gdq-crop>"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

		# If config is RelWithDebInfo, package release files
		COMMAND if $<CONFIG:RelWithDebInfo>==1 (
			"${CMAKE_COMMAND}" -E make_directory
			"${RELEASE_DIR}/data/obs-plugins/gdq-crop"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:RelWithDebInfo>==1 ("${CMAKE_COMMAND}" -E copy_directory
			"${PROJECT_SOURCE_DIR}/data"
			"${RELEASE_DIR}/data/obs-plugins/gdq-crop")

		COMMAND if $<CONFIG:RelWithDebInfo>==1 ("${CMAKE_COMMAND}" -E copy
			"$<TARGET_FILE:gdq-crop>"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:RelWithDebInfo>==1 ("${CMAKE_COMMAND}" -E copy
			"$<TARGET_PDB_FILE:gdq-crop>"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

		# Copy to obs-studio dev environment for immediate testing
		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy
				"$<TARGET_FILE:gdq-crop>"
				"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy
				"$<TARGET_PDB_FILE:gdq-crop>"
				"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E make_directory
				"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/gdq-crop")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/data"
				"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/gdq-crop")
	)
	# --- End of sub-section ---

endif()
# --- End of section ---

# --- Linux-specific build settings and tasks ---
if(UNIX AND NOT APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

	target_compile_options(mbedcrypto PRIVATE -fPIC)
	set_target_properties(gdq-crop PROPERTIES PREFIX "")
	target_link_libraries(gdq-crop
		obs-frontend-api)

	file(GLOB locale_files data/locale/*.ini)

	install(TARGETS gdq-crop
		LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins")
	install(FILES ${locale_files}
		DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/gdq-crop/locale")
endif()
# --- End of section ---

# -- OS X specific build settings and tasks --
if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set(CMAKE_SKIP_RPATH TRUE)
	set_target_properties(gdq-crop PROPERTIES PREFIX "")
	target_link_libraries(gdq-crop "${OBS_FRONTEND_LIB}")
endif()
# -- End of section --
