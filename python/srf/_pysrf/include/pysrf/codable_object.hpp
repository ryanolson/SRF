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

#pragma once

#include "pysrf/types.hpp"
#include "pysrf/utilities/deserializers.hpp"
#include "pysrf/utilities/serializers.hpp"
#include "pysrf/utils.hpp"

#include "srf/codable/codable_protocol.hpp"
#include "srf/codable/encoded_object.hpp"
#include "srf/codable/encoding_options.hpp"
#include "srf/memory/buffer.hpp"
#include "srf/memory/buffer_view.hpp"
#include "srf/memory/memory_kind.hpp"

#include <Python.h>
#include <bytesobject.h>
#include <glog/logging.h>
#include <pybind11/gil.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <cstring>
#include <iomanip>
#include <memory>
#include <type_traits>
#include <typeindex>

namespace srf::codable {

// codable_protocol for just pybind11::object
template <>
struct __attribute__((visibility("default"))) codable_protocol<pybind11::object>
{
    static void serialize(const pybind11::object& py_object,
                          EncodableObject<pybind11::object>& encoded,
                          const EncodingOptions& opts);

    static pybind11::object deserialize(const DecodableObject<pybind11::object>& encoded, std::size_t object_idx);
};

// Default codable_protocol for any object that implements pybind11::object_api (Including PyHolder)
template <typename T>
struct __attribute__((visibility("default")))
codable_protocol<T, std::enable_if_t<pybind11::detail::is_pyobject<T>::value && !std::is_same_v<T, pybind11::object>>>
{
    static void serialize(const T& object, EncodableObject<T>& encoded, const EncodingOptions& opts)
    {
        // Grab the GIL so we can cast
        pybind11::gil_scoped_acquire gil;

        pybind11::object py_object;

        if constexpr (std::is_same_v<T, pysrf::PyHolder>)
        {
            py_object = object.copy_obj();
        }
        else
        {
            py_object = object;
        }

        return codable_protocol<pybind11::object>::serialize(
            std::move(py_object), reinterpret_cast<EncodableObject<pybind11::object>&>(encoded), opts);
    }

    static T deserialize(const DecodableObject<T>& encoded, std::size_t object_idx)
    {
        // Grab the GIL so we can cast on the way out
        pybind11::gil_scoped_acquire gil;

        return codable_protocol<pybind11::object>::deserialize(
            reinterpret_cast<const DecodableObject<pybind11::object>&>(encoded), object_idx);
    }
};

}  // namespace srf::codable
