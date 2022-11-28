#include "pysrf/codable_object.hpp"

namespace srf::codable {

void codable_protocol<pybind11::object>::serialize(const pybind11::object& py_object,
                                                   EncodableObject<pybind11::object>& encoded,
                                                   const EncodingOptions& opts)
{
    using namespace srf::pysrf;

    AcquireGIL gil;

    // pybind11::buffer_info py_bytebuffer;
    // std::tuple<char*, std::size_t> serialized_obj;

    auto guard = encoded.acquire_encoding_context();

    // Serialize the object
    auto serialized_buffer = Serializer::serialize(py_object, opts.use_shm(), !opts.force_copy());

    auto serialized_buffer_info = serialized_buffer.request();

    // Release once we have the buffer_info which is just a C-struct
    gil.release();

    // size_t num_bytes = serialized_mem.attr("nbytes")().cast<size_t>();
    size_t num_bytes = serialized_buffer_info.itemsize * serialized_buffer_info.size;

    // Try to register using a memory view at first. If the object is too small, this may return nullopt
    auto registered_idx = encoded.register_memory_view(
        memory::const_buffer_view{serialized_buffer_info.ptr, num_bytes, memory::memory_kind::host});

    // Check if registering succeeded
    if (!registered_idx.has_value())
    {
        encoded.copy_to_eager_descriptor(
            memory::const_buffer_view{serialized_buffer_info.ptr, num_bytes, memory::memory_kind::host});
        // // Create the memory first
        // auto memory_idx = encoded.create_memory_buffer(num_bytes);

        // auto mutable_memory = encoded.mutable_memory_buffer(memory_idx);

        // std::memcpy(mutable_memory.data(), serialized_buffer_info.ptr, num_bytes);
    }
}

pybind11::object codable_protocol<pybind11::object>::deserialize(const DecodableObject<pybind11::object>& encoded,
                                                                 std::size_t object_idx)
{
    using namespace srf::pysrf;

    // Double check that we dont have the GIL since this yields
    pybind11::gil_scoped_release nogil;

    DCHECK_EQ(std::type_index(typeid(pybind11::object)).hash_code(), encoded.type_index_hash_for_object(object_idx));

    auto idx = encoded.start_idx_for_object(object_idx);

    // Get size of object and create a pybuffer to hold it
    auto buffer_size = encoded.buffer_size(idx);

    // Create a new buffer object with the right size
    // pybind11::bytes dst_buffer = pybind11::bytes("\0") * buffer_size;

    memory::buffer dst_buffer(buffer_size, encoded.host_memory_resource());

    encoded.copy_from_buffer(idx, dst_buffer);

    // Grab the GIL after the data has been acquired
    pybind11::gil_scoped_acquire gil;

    // Convert to a bytes object
    pybind11::bytes dst_bytes = pybind11::bytes(static_cast<const char*>(dst_buffer.data()), dst_buffer.bytes());

    return Deserializer::deserialize(std::move(dst_bytes));
}

// void codable_protocol<pysrf::PyHolder>::serialize(const pysrf::PyHolder& pyholder_object,
//                                                   EncodableObject<pysrf::PyHolder>& encoded,
//                                                   const EncodingOptions& opts)
// {
//     // Grab the GIL so we can access copy_obj()
//     pybind11::gil_scoped_acquire gil;

//     return codable_protocol<pybind11::object>::serialize(
//         pyholder_object.copy_obj(), reinterpret_cast<EncodableObject<pybind11::object>&>(encoded), opts);
// }

// pysrf::PyHolder codable_protocol<pysrf::PyHolder>::deserialize(const DecodableObject<pysrf::PyHolder>& encoded,
//                                                                std::size_t object_idx)
// {
//     return codable_protocol<pybind11::object>::deserialize(
//         reinterpret_cast<const DecodableObject<pybind11::object>&>(encoded), object_idx);
// }

}  // namespace srf::codable
