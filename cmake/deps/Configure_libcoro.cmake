# =============================================================================
# SPDX-FileCopyrightText: Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
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
# =============================================================================

function(find_and_configure_libcoro version)
  list(APPEND CMAKE_MESSAGE_CONTEXT "libcoro")

  rapids_cpm_find(libcoro ${version}
    GLOBAL_TARGETS
      libcoro libcoro::libcoro
    BUILD_EXPORT_SET
      ${PROJECT_NAME}-core-exports
    INSTALL_EXPORT_SET
      ${PROJECT_NAME}-core-exports
    CPM_ARGS
      GIT_REPOSITORY https://github.com/ryanolson/libcoro.git
      GIT_TAG v${version}
      GIT_SHALLOW TRUE
      OPTIONS "BUILD_TESTS OFF"
              "LIBCORO_BUILD_TESTS OFF"
              "LIBCORO_CODE_COVERAGE OFF"
  )
endfunction()

#     GIT_TAG ${version}
find_and_configure_libcoro(${LIBCORO_VERSION})
