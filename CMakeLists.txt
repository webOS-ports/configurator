# Copyright (c) 2007-2024 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)

project(configurator CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC2)
webos_component(3 0 0)

webos_add_compiler_flags(ALL -DMOJ_LINUX -std=c++23)

include_directories(src)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(LUNASERVICE REQUIRED luna-service2)
include_directories(${LUNASERVICE_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE_CFLAGS})


pkg_check_modules(DB8 REQUIRED db8>=3)
include_directories(${DB8_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${DB8_CFLAGS_OTHER})

if (NOT DEFINED USE_PMLOG OR USE_PMLOG)
    # -- check for pmLogLib
    pkg_check_modules(PMLOG REQUIRED PmLogLib>=3)
    include_directories(${PMLOG_INCLUDE_DIRS})
    webos_add_compiler_flags(ALL ${PMLOG_CFLAGS_OTHER} -DUSE_PMLOG)
endif()


if(NO_FIRST_USE)
    add_definitions(-DNO_FIRST_USE)
    message(STATUS "FIRST_USE is not defined")
endif()

find_library(PTHREAD pthread REQUIRED)

# Everything but main(). tests/ builds the same sources into its own copy,
# against a Configurator.h whose stamp cache is a scratch directory.
set(CONFIGURATOR_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/ActivityConfigurator.cpp
    ${PROJECT_SOURCE_DIR}/src/BusClient.cpp
    ${PROJECT_SOURCE_DIR}/src/Configurator.cpp
    ${PROJECT_SOURCE_DIR}/src/DbKindConfigurator.cpp
    ${PROJECT_SOURCE_DIR}/src/DbPermissionsConfigurator.cpp
    ${PROJECT_SOURCE_DIR}/src/FileCacheConfigurator.cpp
    ${PROJECT_SOURCE_DIR}/src/Log.cpp)
add_library(configurator-core OBJECT ${CONFIGURATOR_CORE_SOURCES})

add_executable(configurator $<TARGET_OBJECTS:configurator-core> src/main.cpp)

target_link_libraries(configurator
    ${DB8_LDFLAGS}
    ${GLIB2_LDFLAGS}
    ${LUNASERVICE_LDFLAGS}
    ${PTHREAD}
    ${PMLOG_LDFLAGS}
)

webos_configure_header_files(src)
webos_build_daemon(NAME configurator LAUNCH files/launch)
webos_build_system_bus_files()

# Unit tests, off unless asked for. See tests/README.md.
option(BUILD_TESTING "Build the unit tests in tests/" OFF)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
