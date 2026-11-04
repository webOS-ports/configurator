# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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
#
# The configurator's own sources, built against a Configurator.h whose stamp
# cache is a scratch directory, and the gtest suites in unit/. Nothing here
# goes on the bus. See README.md.

find_package(GTest REQUIRED)

set(CONFIGURATOR_TEST_LOCALSTATEDIR "/tmp/configurator-tests" CACHE STRING
    "Where the tests keep their stamp cache: a runtime path, the same on the build host and on a device")
set(CONFIGURATOR_SANITIZE "" CACHE STRING
    "Sanitizers for the test build, as -fsanitize= takes them (address,undefined or thread)")
set(CONFIGURATOR_NO_SANITIZE "" CACHE STRING
    "Checks to leave out of CONFIGURATOR_SANITIZE, as -fno-sanitize= takes them")

set(WEBOS_INSTALL_LOCALSTATEDIR ${CONFIGURATOR_TEST_LOCALSTATEDIR})
configure_file(${PROJECT_SOURCE_DIR}/src/Configurator.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/gen/Configurator.h @ONLY)

add_library(configurator-test-core OBJECT ${CONFIGURATOR_CORE_SOURCES})
# Ahead of the daemon's generated headers, so the scratch cache path wins.
target_include_directories(configurator-test-core BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

add_executable(configurator-tests
    $<TARGET_OBJECTS:configurator-test-core>
    unit/test_flags.cpp
    unit/test_requests.cpp
    unit/test_run.cpp
    unit/test_scan.cpp
    unit/test_stamps.cpp)

target_include_directories(configurator-tests BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen unit)
target_compile_options(configurator-tests PRIVATE -Wextra)

target_link_libraries(configurator-tests
    GTest::gtest_main
    GTest::gtest
    ${DB8_LDFLAGS}
    ${GLIB2_LDFLAGS}
    ${LUNASERVICE_LDFLAGS}
    ${PTHREAD}
    ${PMLOG_LDFLAGS})

if (CONFIGURATOR_SANITIZE)
    set(CONFIGURATOR_SANITIZE_FLAGS -fsanitize=${CONFIGURATOR_SANITIZE})
    if (CONFIGURATOR_NO_SANITIZE)
        list(APPEND CONFIGURATOR_SANITIZE_FLAGS -fno-sanitize=${CONFIGURATOR_NO_SANITIZE})
    endif()
    foreach(t configurator-test-core configurator-tests)
        target_compile_options(${t} PRIVATE ${CONFIGURATOR_SANITIZE_FLAGS} -fno-omit-frame-pointer
                               -fno-sanitize-recover=all)
    endforeach()
    target_link_options(configurator-tests PRIVATE ${CONFIGURATOR_SANITIZE_FLAGS})
endif()

add_test(NAME configurator-tests COMMAND configurator-tests)
