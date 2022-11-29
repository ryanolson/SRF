/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pysrf/utilities/serializers.hpp"

#include "pysrf/module_wrappers/pickle.hpp"
#include "pysrf/module_wrappers/shared_memory.hpp"

#include <bytesobject.h>
#include <glog/logging.h>
#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>  // IWYU pragma: keep
#include <pybind11/pytypes.h>

#include <cstdlib>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

namespace py = pybind11;
namespace srf::pysrf {

pybind11::object Serializer::persist_to_shared_memory(pybind11::object obj)
{
    VLOG(8) << "Persisting object to shared memory: " << py::cast<std::string>(repr(obj));
    auto pkl   = PythonPickleInterface();
    auto shmem = PythonSharedMemoryInterface();
    auto bytes = pkl.pickle(obj);

    shmem.allocate(pybind11::len(bytes));
    shmem.set(bytes);

    auto descriptor = build_shmem_descriptor(shmem);
    shmem.close();

    VLOG(8) << "Finished persisting object to shared memory: " << py::cast<std::string>(repr(obj));
    return descriptor;
}

py::buffer Serializer::serialize(pybind11::object obj, bool use_shmem, bool return_raw_buffer)
{
    if (use_shmem)
    {
        obj = persist_to_shared_memory(obj);
    }

    auto pkl      = PythonPickleInterface();
    auto py_bytes = pkl.pickle(obj);

    // Convert the bytes to a buffer object
    return py::buffer(std::move(py_bytes));
}
}  // namespace srf::pysrf
