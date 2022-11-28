
#include "srf/pubsub/client_subscription_base.hpp"

#include "internal/pubsub/publisher_manager.hpp"
#include "internal/remote_descriptor/encoded_object.hpp"
#include "internal/runtime/runtime.hpp"
#include "internal/utils/contains.hpp"
#include "internal/utils/ranges.hpp"

#include "srf/core/runtime.hpp"
#include "srf/node/type_traits.hpp"
#include "srf/pubsub/state.hpp"
#include "srf/types.hpp"
#include "srf/utils/string_utils.hpp"

#include <glog/logging.h>

#include <utility>

namespace srf::pubsub {

ClientSubscriptionBaseChangeHandle::ClientSubscriptionBaseChangeHandle(ClientSubscriptionBase* parent,
                                                                       size_t handle_id) :
  m_parent(parent),
  m_handle_id(handle_id)
{}

ClientSubscriptionBaseChangeHandle::ClientSubscriptionBaseChangeHandle(ClientSubscriptionBaseChangeHandle&& other) :
  m_parent(std::move(other.m_parent)),
  m_handle_id(std::move(other.m_handle_id))
{
    VLOG(10) << "Moving handle";
}

ClientSubscriptionBaseChangeHandle::~ClientSubscriptionBaseChangeHandle()
{
    this->release();
}

ClientSubscriptionBaseChangeHandle& ClientSubscriptionBaseChangeHandle::operator=(
    ClientSubscriptionBaseChangeHandle&& other)
{
    std::swap(m_parent, other.m_parent);
    std::swap(m_handle_id, other.m_handle_id);

    return *this;
}

void ClientSubscriptionBaseChangeHandle::release()
{
    if (m_parent != nullptr)
    {
        m_parent->release_connection_changed_handler(m_handle_id);
    }
}

ClientSubscriptionBase::ClientSubscriptionBase(std::string service_name, core::IRuntime& runtime) :
  m_service_name(std::move(service_name)),
  m_runtime(runtime)
{
    m_join_future = m_service_completed_promise.get_future().share();
}

ClientSubscriptionBase::~ClientSubscriptionBase()
{
    // Make sure close was called
    this->close();

    // Release any waiters
    m_tagged_cv.notify_all();
}

const std::string& ClientSubscriptionBase::service_name() const
{
    return m_service_name;
}

const std::uint64_t& ClientSubscriptionBase::tag() const
{
    return m_tag;
}

// std::unique_ptr<runnable::Runner> PublisherBase::link_service(
//     std::uint64_t tag,
//     std::function<void()> drop_service_fn,
//     runnable::LaunchControl& launch_control,
//     runnable::LaunchOptions& launch_options,
//     node::SinkProperties<std::pair<std::uint64_t, std::unique_ptr<srf::remote_descriptor::Storage>>>& data_sink)
// {
//     // Save the tag
//     m_tag             = tag;
//     m_drop_service_fn = drop_service_fn;

//     return this->do_link_service(tag, std::move(drop_service_fn), launch_control, launch_options, data_sink);
// }

ClientSubscriptionBaseChangeHandle ClientSubscriptionBase::register_connections_changed_handler(
    connections_changed_handler_t on_changed_fn)
{
    // Lock while changing state
    std::unique_lock lock(m_mutex);

    auto handle_id = m_handle_increment++;

    m_on_connections_changed_fns.emplace(
        std::pair<size_t, connections_changed_handler_t>(handle_id, std::move(on_changed_fn)));

    return ClientSubscriptionBaseChangeHandle(this, handle_id);
}

void ClientSubscriptionBase::close()
{
    // Only call this function once
    std::call_once(m_drop_service_fn_called, m_drop_service_fn);
}

void ClientSubscriptionBase::await_join()
{
    m_join_future.get();
}

size_t ClientSubscriptionBase::await_connections()
{
    std::unique_lock lock(m_mutex);

    m_tagged_cv.wait(lock, [this]() {
        // Wait for instances to be ready
        return !this->get_tagged_instances().empty() || m_state == SubscriptionState::Completed;
    });

    return this->get_tagged_instances().size();
}

void ClientSubscriptionBase::await_completed()
{
    std::unique_lock lock(m_mutex);

    m_tagged_cv.wait(lock, [this]() {
        // Wait for instances to be ready
        return m_state == SubscriptionState::Completed;
    });
}

void ClientSubscriptionBase::set_linked_service(std::uint64_t tag, std::function<void()> drop_service_fn)
{
    m_tag             = tag;
    m_drop_service_fn = drop_service_fn;
}

const std::unordered_map<TagID, InstanceID>& ClientSubscriptionBase::get_tagged_instances() const
{
    return m_active_tagged_instances;
}

std::unique_ptr<codable::EncodedObject> ClientSubscriptionBase::get_encoded_obj() const
{
    // Cast our public runtime into the internal runtime
    auto& runtime = dynamic_cast<internal::runtime::Runtime&>(m_runtime);

    // Build an internal encoded object and return
    auto encoded_obj = std::make_unique<internal::remote_descriptor::EncodedObject>(runtime.resources());

    return encoded_obj;
}

void ClientSubscriptionBase::on_tagged_instances_updated()
{
    // Do nothing in base
}

void ClientSubscriptionBase::update_tagged_members(SubscriptionState state, const tagged_members_t& tagged_members)
{
    // Lock while changing state
    std::unique_lock lock(m_mutex);

    m_tagged_members = tagged_members;
    m_state          = state;

    m_active_tagged_instances.clear();

    for (const auto& [tag_id, member] : tagged_members)
    {
        if (member.state == SubscriptionState::Connected)
        {
            m_active_tagged_instances[member.tag] = member.instance_id;
        }
    }

    DVLOG(10) << "ClientSubscription: '" << this->service_name() << "/" << this->role()
              << "' updated tagged instances. New State: " << (int)state << ", Tags: "
              << utils::StringUtil::array_to_str(begin_keys(m_active_tagged_instances),
                                                 end_keys(m_active_tagged_instances));

    this->on_tagged_instances_updated();

    // Call the on_changed handlers
    for (auto& [change_id, change_fn] : m_on_connections_changed_fns)
    {
        change_fn(m_tagged_members);
    }

    m_tagged_cv.notify_all();
}

void ClientSubscriptionBase::release_connection_changed_handler(size_t handle_id)
{
    std::unique_lock lock(m_mutex);

    m_on_connections_changed_fns.erase(handle_id);
}

}  // namespace srf::pubsub
