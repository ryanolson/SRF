#include "srf/manifold/egress.hpp"

namespace srf::manifold {

void EgressDelegate::add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink)
{
    std::unique_lock lock(m_mutex);

    if (m_connected_addresses.find(address) != m_connected_addresses.end())
    {
        // Dont re-remove the same input
        return;
    }

    this->do_add_output(address, output_sink);

    m_connected_addresses.insert(address);
}

void EgressDelegate::remove_output(const SegmentAddress& address)
{
    std::unique_lock lock(m_mutex);

    if (m_connected_addresses.find(address) == m_connected_addresses.end())
    {
        // Dont re-remove the same input
        return;
    }

    do_remove_output(address);

    m_connected_addresses.erase(address);
}

boost::fibers::mutex& EgressDelegate::mutex()
{
    return m_mutex;
}

const std::set<SegmentAddress>& EgressDelegate::connected_addresses() const
{
    return m_connected_addresses;
}

}  // namespace srf::manifold
