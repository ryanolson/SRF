
#include "srf/pubsub/subscriber.hpp"

#include "internal/pubsub/subscriber_manager.hpp"
#include "internal/runtime/runtime.hpp"

#include "srf/core/runtime.hpp"

#include <glog/logging.h>

namespace srf::pubsub {
SubscriberBase::SubscriberBase(std::string service_name, core::IRuntime& runtime) :
  m_service_name(std::move(service_name)),
  m_runtime(runtime)
{}

const std::string& SubscriberBase::service_name() const
{
    return m_service_name;
}

const std::uint64_t& SubscriberBase::tag() const
{
    return m_tag;
}

void SubscriberBase::link_service(std::uint64_t tag, std::function<void()> drop_service_fn)
{
    // Save the tag
    m_tag = tag;

    this->do_link_service(tag, std::move(drop_service_fn));
}

void SubscriberBase::link_service(std::uint64_t tag,
                                  std::function<void()> drop_service_fn,
                                  node::SourceProperties<std::unique_ptr<codable::EncodedObject>>& source)
{
    // Save the tag
    m_tag = tag;

    return this->do_link_service(tag, std::move(drop_service_fn), source);
}

void SubscriberBase::update_tagged_instances(const std::unordered_map<std::uint64_t, InstanceID>& tagged_instances)
{
    m_tagged_instances = tagged_instances;

    this->on_tagged_instances_updated();

    // Call the on_changed handlers
    for (auto& change_fn : m_on_connections_changed_fns)
    {
        change_fn(m_tagged_instances);
    }
}

void SubscriberBase::register_connections_changed_handler(connections_changed_handler_t on_changed_fn)
{
    m_on_connections_changed_fns.emplace_back(std::move(on_changed_fn));
}

const std::unordered_map<std::uint64_t, InstanceID>& SubscriberBase::get_tagged_instances() const
{
    return m_tagged_instances;
}

std::unique_ptr<codable::EncodedObject> SubscriberBase::get_encoded_obj() const
{
    // Cast our public runtime into the internal runtime
    auto& runtime = dynamic_cast<internal::runtime::Runtime&>(m_runtime);

    // Build an internal encoded object and return
    auto encoded_obj = std::make_unique<internal::remote_descriptor::EncodedObject>(runtime.resources());

    return encoded_obj;
}

void SubscriberBase::push_object(std::uint64_t id, std::unique_ptr<remote_descriptor::Storage> storage)
{
    LOG(INFO) << "subscriber writing object";

    auto& runtime = dynamic_cast<internal::runtime::Runtime&>(m_runtime);

    DCHECK(runtime.runnable().main().caller_on_same_thread());

    auto found = m_tagged_instances.find(id);

    CHECK(found != m_tagged_instances.end()) << "Tagged ID must be in the list of available instances";

    internal::data_plane::RemoteDescriptorMessage msg;

    msg.tag = id;

    // TODO(MDD): Figure out a better way to get the endpoint
    msg.endpoint = runtime.resources().network()->data_plane().client().endpoint_shared(found->second);

    msg.rd = runtime.remote_descriptor_manager().store_object(std::move(storage));
    CHECK(runtime.resources().network()->data_plane().client().remote_descriptor_channel().await_write(
              std::move(msg)) == channel::Status::success);
}

void SubscriberBase::on_tagged_instances_updated()
{
    // Do nothing in base
}

const std::string& SubscriberEdgeBase::service_name()
{
    return m_parent.service_name();
}
const std::uint64_t& SubscriberEdgeBase::tag()
{
    return m_parent.tag();
}
void SubscriberEdgeBase::register_connections_changed_handler(
    SubscriberBase::connections_changed_handler_t on_changed_fn)
{
    m_parent.register_connections_changed_handler(std::move(on_changed_fn));
}
SubscriberEdgeBase::SubscriberEdgeBase(SubscriberBase& parent) : m_parent(parent) {}

void make_pub_service(std::unique_ptr<SubscriberBase> subscriber, core::IRuntime& runtime)
{
    // Cast the runtime to the internal runtime
    auto& internal_runtime = dynamic_cast<internal::runtime::Runtime&>(runtime);

    // Create the new service
    std::unique_ptr<internal::pubsub::SubscriberManager> manager =
        std::make_unique<internal::pubsub::SubscriberManager>(std::move(subscriber), internal_runtime);

    // Start the service
    internal_runtime.resources().network()->control_plane().register_subscription_service(std::move(manager));

    // // Capture the drop_service lambda
    // auto drop_service_fn = manager->get_drop_service_fn();

    // return drop_service_fn;
}

void SubscriberBase::main(runnable::Context& context)
{
    m_running = true;

    // Need to loop until m_running = false
    while (m_running)
    {
        // Call function to pull next item
    }
}
void SubscriberBase::on_state_update(const state_t& state) {}

}  // namespace srf::pubsub
