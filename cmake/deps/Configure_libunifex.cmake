#=============================================================================
# SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#=============================================================================

function(find_and_configure_libunifex version)

  list(APPEND CMAKE_MESSAGE_CONTEXT "libunifex")

  # Needs a patch to change the internal tracker to use fiber specific storage instead of TSS
  # rapids_cpm_find(libunifex $
  rapids_cpm_find(libunifex ${version}
    GLOBAL_TARGETS
      unifex
    BUILD_EXPORT_SET
      ${PROJECT_NAME}-core-exports
    INSTALL_EXPORT_SET
      ${PROJECT_NAME}-core-exports
    CPM_ARGS
      GIT_REPOSITORY  https://github.com/facebookexperimental/libunifex.git
      GIT_TAG         "591ec09e7d51858ad05be979d4034574215f5971"
      GIT_SHALLOW     TRUE
      OPTIONS         "UNIFEX_BUILD_EXAMPLES OFF"
                      "UNIFEX_NO_LIBURING ON"
                      "CMAKE_CXX_STANDARD 20"
  )
endfunction()

find_and_configure_libunifex(${LIBUNIFEX_VERSION})
