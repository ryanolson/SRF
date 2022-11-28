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

#include "pysrf/module_wrappers/pickle.hpp"

#include "pysrf/utilities/object_cache.hpp"

#include <glog/logging.h>
#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <array>
#include <memory>
#include <ostream>

namespace py = pybind11;
namespace srf::pysrf {

using namespace pybind11::literals;

PythonPickleInterface::~PythonPickleInterface() = default;

PythonPickleInterface::PythonPickleInterface() : m_pycache(PythonObjectCache::get_handle())
{
    auto pickle_mod = m_pycache.get_module("pickle");

    m_func_load =
        m_pycache.get_or_load("PythonPickleInterface.load", [pickle_mod]() { return pickle_mod.attr("load"); });
    m_func_loads =
        m_pycache.get_or_load("PythonPickleInterface.loads", [pickle_mod]() { return pickle_mod.attr("loads"); });
    m_func_dump =
        m_pycache.get_or_load("PythonPickleInterface.dump", [pickle_mod]() { return pickle_mod.attr("dump"); });
    m_func_dumps =
        m_pycache.get_or_load("PythonPickleInterface.dumps", [pickle_mod]() { return pickle_mod.attr("dumps"); });

    auto io_mod = m_pycache.get_module("io");

    m_bytes_io = m_pycache.get_or_load("io.BytesIO", [io_mod]() { return io_mod.attr("BytesIO"); });
}

pybind11::bytes PythonPickleInterface::pickle(pybind11::object obj)
{
    try
    {
        // auto buffer_callback = [](pybind11::object pickle_buffer) {
        //     pybind11::memoryview mem_view = pickle_buffer.attr("raw")();
        // };

        return m_func_dumps(obj, "protocol"_a = 5);

        // // Create an instance of a io.BytesIO
        // py::object io_bytes = m_bytes_io();

        // m_func_dump(obj, "file"_a = io_bytes, "protocol"_a = 5);

        // // Returns a memoryview of the underlying data
        // return io_bytes.attr("getbuffer")();

    } catch (pybind11::error_already_set err)
    {
        LOG(ERROR) << "Object serialization failed: " << err.what();
        throw;
    }
}

pybind11::object PythonPickleInterface::unpickle(pybind11::bytes bytes)
{
    try
    {
        return m_func_loads(std::move(bytes));
    } catch (pybind11::error_already_set err)
    {
        LOG(ERROR) << "Object deserialization failed: " << err.what();
        throw;
    }
}

}  // namespace srf::pysrf
