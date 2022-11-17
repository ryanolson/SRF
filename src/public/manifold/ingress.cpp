#include "srf/manifold/ingress.hpp"

namespace srf::manifold {

void IngressDelegate::add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source)
{
    if (m_connected_addresses.find(address) != m_connected_addresses.end())
    {
        // Dont re-add the same input
        return;
    }

    do_add_input(address, input_source);

    m_connected_addresses.insert(address);
}

void IngressDelegate::remove_input(const SegmentAddress& address)
{
    if (m_connected_addresses.find(address) == m_connected_addresses.end())
    {
        // Dont re-remove the same input
        return;
    }

    do_remove_input(address);

    m_connected_addresses.erase(address);
}

const std::set<SegmentAddress>& IngressDelegate::connected_addresses() const
{
    return m_connected_addresses;
}

}  // namespace srf::manifold
