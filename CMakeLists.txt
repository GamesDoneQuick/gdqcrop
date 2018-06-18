cmake_minimum_required(VERSION 3.2)
project(gdq-crop)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(LIBOBS_INCLUDE_DIR "LIBOBS_PATH_NOT_FOUND" CACHE PATH "location of the libobs subfolder in the source code of OBS Studio")
set(LIBOBS_LIB  "LIBOBS_FILE_NOT_FOUND" CACHE FILEPATH "location of the obs.lib file")

find_package(LibObs REQUIRED)

set(ENABLE_PROGRAMS false)

set(gdq-crop_SOURCES
	gdq-crop.c)

set(gdq-crop_HEADERS)

# --- Platform-independent build settings ---
add_library(gdq-crop MODULE 
	${gdq-crop_SOURCES}
	${gdq-crop_HEADERS})

target_link_libraries(gdq-crop 
	libobs)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ARCH_NAME "64bit")
	set(OBS_BUILDDIR_ARCH "build64")
else()
	set(ARCH_NAME "32bit")
	set(OBS_BUILDDIR_ARCH "build32")
endif()

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
