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

#include "test_pysrf.hpp"

#include "pysrf/codable_object.hpp"  // IWYU pragma: keep
#include "pysrf/forward.hpp"
#include "pysrf/types.hpp"

#include "srf/codable/api.hpp"
#include "srf/codable/codable_protocol.hpp"
#include "srf/codable/decode.hpp"
#include "srf/codable/encode.hpp"
#include "srf/codable/encoded_object.hpp"
#include "srf/codable/encoding_options.hpp"
#include "srf/codable/type_traits.hpp"
#include "srf/memory/resources/host/malloc_memory_resource.hpp"
#include "srf/protos/codable.pb.h"

#include <gtest/gtest.h>
#include <object.h>
#include <pybind11/cast.h>
#include <pybind11/gil.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <string>  // IWYU pragma: keep

// IWYU pragma: no_include <gtest/gtest-message.h>
// IWYU pragma: no_include "gtest/gtest_pred_impl.h"
// IWYU pragma: no_include <gtest/gtest-test-part.h>
// IWYU pragma: no_include <pybind11/detail/common.h>
// IWYU pragma: no_include <tupleobject.h>

namespace py    = pybind11;
namespace pysrf = srf::pysrf;
using namespace std::string_literals;
using namespace pybind11::literals;

namespace srf {

class TestCodableStorage : public srf::codable::ICodableStorage
{
  public:
    TestCodableStorage() : m_host_mr(std::make_shared<srf::memory::malloc_memory_resource>()) {}

    /**
     * @brief ObjectDescriptor describing the encoded object.
     * @return const protos::ObjectDescriptor&
     */
    const codable::protos::EncodedObject& proto() const override;

    /**
     * @brief The number of unqiue objects described by the encoded object
     * @return std::size_t
     */
    obj_idx_t object_count() const override;

    /**
     * @brief The number of unique memory regions contained in the multiple part descriptor.
     * @return std::size_t
     */
    idx_t descriptor_count() const override;

    /**
     * @brief Hash of std::type_index for the object at idx
     *
     * @param object_idx
     * @return std::type_index
     */
    std::size_t type_index_hash_for_object(const obj_idx_t& object_idx) const override;

    /**
     * @brief Starting index of object at idx
     *
     * @param object_idx
     * @return idx_t
     */
    idx_t start_idx_for_object(const obj_idx_t& object_idx) const override;

    /**
     * @brief Parent for object at idx
     *
     * @return std::optional<obj_idx_t> - if nullopt, then the object is a top-level object; otherwise, it is a child
     * object with a parent object at the returned value
     */
    std::optional<obj_idx_t> parent_obj_idx_for_object(const obj_idx_t& object_idx) const override;

    // register memory region
    // may return nullopt if the region is considered too small
    std::optional<codable::idx_t> register_memory_view(srf::memory::const_buffer_view view,
                                                       bool force_register = false) final
    {
        return this->copy_to_eager_descriptor(view);
    }

    // copy to eager descriptor
    codable::idx_t copy_to_eager_descriptor(srf::memory::const_buffer_view view) final
    {
        CHECK(context_acquired());

        auto count = descriptor_count();
        auto* desc = mutable_proto().add_descriptors()->mutable_eager_desc();
        desc->set_data(view.data(), view.bytes());
        return count;
    }

    idx_t add_meta_data(const google::protobuf::Message& meta_data) override;

    // create a buffer owned by this
    codable::idx_t create_memory_buffer(std::size_t bytes) final
    {
        CHECK(context_acquired());

        auto buffer = srf::memory::buffer(bytes, this->host_memory_resource());
        auto idx    = register_memory_view(buffer);
        CHECK(idx);
        m_buffers[*idx] = std::move(buffer);
        return *idx;
    }

    void copy_to_buffer(idx_t buffer_idx, memory::const_buffer_view view) override;

    // access a buffer created from
    memory::buffer_view mutable_host_buffer_view(const idx_t& buffer_idx) override
    {
        CHECK(context_acquired());

        auto search = m_buffers.find(buffer_idx);
        CHECK(search != m_buffers.end());
        return search->second;
    }

    bool context_acquired() const override;

  protected:
    void copy_from_buffer(const codable::idx_t& idx, srf::memory::buffer_view dst_view) const final
    {
        CHECK_LT(idx, descriptor_count());
        const auto& desc = proto().descriptors().at(idx);

        const auto& eager_buffer = proto().descriptors().at(idx).eager_desc();
        CHECK_LE(dst_view.bytes(), eager_buffer.data().size());

        if (dst_view.kind() == srf::memory::memory_kind::device)
        {
            LOG(FATAL) << "implement async device copies";
        }

        if (dst_view.kind() == srf::memory::memory_kind::none)
        {
            LOG(WARNING) << "got a memory::kind::none";
        }
        std::memcpy(dst_view.data(), eager_buffer.data().data(), dst_view.bytes());
    }

    std::size_t buffer_size(const idx_t& idx) const override;

    std::shared_ptr<srf::memory::memory_resource> host_memory_resource() const final
    {
        return m_host_mr;
    }
    std::shared_ptr<srf::memory::memory_resource> device_memory_resource() const final
    {
        return m_device_mr;
    }

  private:
    codable::protos::EncodedObject& mutable_proto() override;

    obj_idx_t push_context(std::type_index type_index) override;
    void pop_context(obj_idx_t object_idx) override;

    std::map<codable::idx_t, srf::memory::buffer> m_buffers;

    std::shared_ptr<srf::memory::memory_resource> m_host_mr;
    std::shared_ptr<srf::memory::memory_resource> m_device_mr{nullptr};
};

}  // namespace srf

PYSRF_TEST_CLASS(CodablePyobject);

TEST_F(TestCodablePyobject, PyObject)
{
    // Verify the main object types
    static_assert(codable::is_codable_v<py::object>, "pybind11::object should be codable.");
    static_assert(codable::is_codable_v<pysrf::PyHolder>, "pysrf::PyHolder should be codable.");
    static_assert(codable::is_codable_v<pysrf::PyObjectHolder>, "pysrf::PyObjectHolder should be codable.");
    static_assert(codable::is_codable_v<pysrf::PyObjectWrapper>, "pysrf::PyObjectWrapper should be codable.");

    // Check a couple of common pybind11 types
    static_assert(codable::is_codable_v<py::dict>, "py::dict should be codable.");
    static_assert(codable::is_codable_v<py::list>, "py::list should be codable.");
    static_assert(codable::is_codable_v<py::str>, "py::str should be codable.");
    static_assert(codable::is_codable_v<py::int_>, "py::int_ should be codable.");

    static_assert(!codable::is_codable_v<PyObject>,
                  "No support for directly coding cpython objects -- "
                  "use pybind11::object or srf::PyHolder");
}

TEST_F(TestCodablePyobject, EncodedObjectSimple)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    py::object py_dict = py::dict("prop1"_a = py::none(),
                                  "prop2"_a = 123,
                                  "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                                  "prop4"_a = py::bool_(false),
                                  "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);
    codable::encode(py_dict, enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    py::dict py_dict_deserialized = codable::decode<py::object>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, EncodedHolderObjectSimple)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    pysrf::PyHolder py_dict = py::dict("prop1"_a = py::none(),
                                       "prop2"_a = 123,
                                       "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                                       "prop4"_a = py::bool_(false),
                                       "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);
    codable::encode(py_dict, enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    pysrf::PyHolder py_dict_deserialized = codable::decode<pysrf::PyHolder>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.copy_obj().equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, EncodedObjectSharedMem)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    py::dict py_dict("prop1"_a = py::none(),
                     "prop2"_a = 123,
                     "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                     "prop4"_a = py::bool_(false),
                     "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, true);
    codable::encode(py_dict, enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    py::dict py_dict_deserialized = codable::decode<py::object>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, EncodedHolderObjectSharedMem)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    pysrf::PyHolder py_dict = py::dict("prop1"_a = py::none(),
                                       "prop2"_a = 123,
                                       "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                                       "prop4"_a = py::bool_(false),
                                       "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, true);
    codable::encode(py_dict, enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    pysrf::PyHolder py_dict_deserialized = codable::decode<pysrf::PyHolder>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.copy_obj().equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, EncodedObjectSharedMemNoCopy)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    py::dict py_dict("prop1"_a = py::none(),
                     "prop2"_a = 123,
                     "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                     "prop4"_a = py::bool_(false),
                     "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);
    codable::encode(py::cast<py::object>(py_dict), enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    py::dict py_dict_deserialized = codable::decode<py::object>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, EncodedHolderObjectSharedMemNoCopy)
{
    py::gil_scoped_acquire gil;

    py::module_ mod = py::module_::import("os");

    pysrf::PyHolder py_dict = py::dict("prop1"_a = py::none(),
                                       "prop2"_a = 123,
                                       "prop3"_a = py::dict("subprop1"_a = 1, "subprop2"_a = "abc"),
                                       "prop4"_a = py::bool_(false),
                                       "func"_a  = py::getattr(mod, "getuid"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);
    codable::encode(py_dict, enc_obj, enc_ops);

    EXPECT_EQ(enc_obj.object_count(), 1);
    EXPECT_EQ(enc_obj.descriptor_count(), 1);

    pysrf::PyHolder py_dict_deserialized = codable::decode<pysrf::PyHolder>(enc_obj);

    EXPECT_TRUE(!py_dict_deserialized.copy_obj().equal(py::dict()));
    EXPECT_TRUE(py_dict_deserialized.equal(py_dict));
    EXPECT_TRUE(py_dict_deserialized["prop3"].equal(py_dict["prop3"]));
}

TEST_F(TestCodablePyobject, BadUnpickleable)
{
    py::gil_scoped_acquire gil;
    py::dict py_dict("mod(unpickleable)"_a = py::module_::import("sys"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);

    EXPECT_THROW(codable::encode(py::cast<py::object>(py_dict), enc_obj, enc_ops), py::error_already_set);
}

TEST_F(TestCodablePyobject, BadHolderUnpickleable)
{
    py::gil_scoped_acquire gil;
    pysrf::PyHolder py_dict = py::dict("mod(unpickleable)"_a = py::module_::import("sys"));

    TestCodableStorage enc_obj;
    codable::EncodingOptions enc_ops(true, false);

    EXPECT_THROW(codable::encode(py_dict, enc_obj, enc_ops), py::error_already_set);
}
