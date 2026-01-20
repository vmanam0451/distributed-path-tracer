// #include "zmq_peer.hpp"
// #include <arpa/inet.h>
// #include <aws/core/Aws.h>
// #include <aws/servicediscovery/ServiceDiscoveryClient.h>
// #include <aws/servicediscovery/model/DeregisterInstanceRequest.h>
// #include <aws/servicediscovery/model/ListInstancesRequest.h>
// #include <aws/servicediscovery/model/RegisterInstanceRequest.h>
// #include <ifaddrs.h>
// #include <net/if.h>
// #include <nlohmann/json.hpp>
// #include <spdlog/spdlog.h>

// using json = nlohmann::json;

// namespace cloud
// {

// zmq_peer::zmq_peer(const std::string &peer_id, uint16_t port)
//     : m_peer_id(peer_id), m_port(port), m_context(1), m_socket(m_context, zmq::socket_type::router)
// {
//     // Set socket identity to our peer ID - this is how other peers will address us
//     m_socket.set(zmq::sockopt::routing_id, peer_id);

//     // Configure socket options for reliability
//     m_socket.set(zmq::sockopt::linger, 1000);     // Wait 1s on close for pending messages
//     m_socket.set(zmq::sockopt::sndhwm, 100000);   // High water mark for outbound
//     m_socket.set(zmq::sockopt::rcvhwm, 100000);   // High water mark for inbound
//     m_socket.set(zmq::sockopt::tcp_keepalive, 1); // Enable TCP keepalive
//     m_socket.set(zmq::sockopt::tcp_keepalive_idle, 60);
//     m_socket.set(zmq::sockopt::tcp_keepalive_intvl, 10);
//     m_socket.set(zmq::sockopt::reconnect_ivl, 100);      // Reconnect interval 100ms
//     m_socket.set(zmq::sockopt::reconnect_ivl_max, 5000); // Max reconnect interval 5s

//     // ROUTER socket will silently drop messages to unknown peers (mandatory = 0)
//     // Set to 1 to get errors instead - useful for debugging
//     m_socket.set(zmq::sockopt::router_mandatory, 0);
// }

// zmq_peer::~zmq_peer()
// {
//     stop();
// }

// void zmq_peer::start(const std::string &namespace_name, const std::string &service_name, const std::string
// &service_id,
//                      int expected_peers, const std::string &aws_region)
// {
//     m_running = true;
//     m_service_id = service_id;
//     m_expected_peers = expected_peers;
//     m_aws_region = aws_region;

//     // Bind the ROUTER socket to accept incoming connections
//     std::string bind_endpoint = "tcp://*:" + std::to_string(m_port);
//     m_socket.bind(bind_endpoint);
//     spdlog::info("ZMQ peer '{}' bound to port {}", m_peer_id, m_port);

//     // Start receiver thread
//     m_receiver_thread = std::thread(&zmq_peer::receiver_thread_fn, this);

//     // Start sender thread
//     m_sender_thread = std::thread(&zmq_peer::sender_thread_fn, this);

//     // Register with Cloud Map and discover peers if configured
//     if (!namespace_name.empty() && !service_name.empty())
//     {
//         register_instance(namespace_name, service_name);
//         discover_peers(namespace_name, service_name);
//     }

//     spdlog::info("ZMQ peer '{}' started", m_peer_id);
// }

// void zmq_peer::stop()
// {
//     if (!m_running.exchange(false))
//         return;

//     spdlog::info("Stopping ZMQ peer '{}'", m_peer_id);

//     // Wait for discovery thread
//     if (m_discovery_thread.joinable())
//     {
//         m_discovery_thread.join();
//     }

//     // Deregister from Cloud Map
//     deregister_instance();

//     // Wait for threads to finish
//     if (m_receiver_thread.joinable())
//     {
//         m_receiver_thread.join();
//     }
//     if (m_sender_thread.joinable())
//     {
//         m_sender_thread.join();
//     }

//     // Close socket
//     {
//         std::lock_guard<std::mutex> lock(m_socket_mutex);
//         m_socket.close();
//     }

//     spdlog::info("ZMQ peer '{}' stopped", m_peer_id);
// }

// void zmq_peer::add_peer(const std::string &peer_id, const std::string &host, uint16_t port)
// {
//     if (peer_id == m_peer_id)
//         return; // Don't connect to ourselves

//     std::string endpoint = "tcp://" + host + ":" + std::to_string(port);

//     {
//         std::lock_guard<std::mutex> lock(m_peers_mutex);

//         // Check if already exists
//         if (m_peers.find(peer_id) != m_peers.end())
//         {
//             spdlog::debug("Peer '{}' already known", peer_id);
//             return;
//         }

//         // Add new peer
//         auto conn = std::make_shared<peer_connection>();
//         conn->peer_id = peer_id;
//         conn->endpoint = endpoint;
//         conn->connected = false;
//         m_peers[peer_id] = conn;
//     }

//     spdlog::info("Added peer '{}' at {}", peer_id, endpoint);
//     connect_to_peer(peer_id, endpoint);
// }

// void zmq_peer::connect_to_peer(const std::string &peer_id, const std::string &endpoint)
// {
//     try
//     {
//         {
//             std::lock_guard<std::mutex> lock(m_socket_mutex);
//             // ROUTER socket can connect to multiple endpoints
//             m_socket.connect(endpoint);
//         }

//         // Mark as connected
//         {
//             std::lock_guard<std::mutex> lock(m_peers_mutex);
//             auto it = m_peers.find(peer_id);
//             if (it != m_peers.end())
//             {
//                 it->second->connected = true;
//             }
//         }

//         spdlog::info("Connected to peer '{}' at {}", peer_id, endpoint);

//         // Send announcement so the peer knows our identity
//         std::vector<uint8_t> announce_data(m_peer_id.begin(), m_peer_id.end());
//         send_message(peer_id, ZmqMessageType::PEER_ANNOUNCE, announce_data);
//     }
//     catch (const zmq::error_t &e)
//     {
//         spdlog::error("Failed to connect to peer '{}' at {}: {}", peer_id, endpoint, e.what());
//     }
// }

// void zmq_peer::receiver_thread_fn()
// {
//     spdlog::debug("Receiver thread started for peer '{}'", m_peer_id);

//     zmq::pollitem_t items[] = {{m_socket, 0, ZMQ_POLLIN, 0}};

//     while (m_running)
//     {
//         try
//         {
//             // Poll with timeout so we can check m_running
//             zmq::poll(items, 1, std::chrono::milliseconds(100));

//             if (items[0].revents & ZMQ_POLLIN)
//             {
//                 std::vector<zmq::message_t> frames;

//                 // Receive all frames of the multipart message
//                 zmq::recv_result_t result;
//                 do
//                 {
//                     zmq::message_t frame;
//                     {
//                         std::lock_guard<std::mutex> lock(m_socket_mutex);
//                         result = m_socket.recv(frame, zmq::recv_flags::none);
//                     }
//                     if (result)
//                     {
//                         frames.push_back(std::move(frame));
//                     }

//                     // Check if more frames are coming
//                     int more = 0;
//                     size_t more_size = sizeof(more);
//                     {
//                         std::lock_guard<std::mutex> lock(m_socket_mutex);
//                         m_socket.getsockopt(ZMQ_RCVMORE, &more, &more_size);
//                     }
//                     if (!more)
//                         break;
//                 } while (m_running);

//                 // ROUTER socket frames: [identity, empty, type, data]
//                 // Or for connected peers: [identity, type, data]
//                 if (frames.size() >= 3)
//                 {
//                     std::string sender_id(static_cast<char *>(frames[0].data()), frames[0].size());

//                     // Find type and data frames (handle optional empty delimiter)
//                     size_t type_idx = 1;
//                     if (frames[type_idx].size() == 0 && frames.size() >= 4)
//                     {
//                         type_idx = 2; // Skip empty delimiter frame
//                     }

//                     if (type_idx < frames.size())
//                     {
//                         uint8_t type_byte = *static_cast<uint8_t *>(frames[type_idx].data());
//                         ZmqMessageType type = static_cast<ZmqMessageType>(type_byte);

//                         std::vector<uint8_t> data;
//                         if (type_idx + 1 < frames.size())
//                         {
//                             auto &data_frame = frames[type_idx + 1];
//                             data.assign(static_cast<uint8_t *>(data_frame.data()),
//                                         static_cast<uint8_t *>(data_frame.data()) + data_frame.size());
//                         }

//                         process_message(sender_id, type, data);
//                     }
//                 }
//             }
//         }
//         catch (const zmq::error_t &e)
//         {
//             if (m_running && e.num() != ETERM)
//             {
//                 spdlog::error("Receiver error: {}", e.what());
//             }
//         }
//     }

//     spdlog::debug("Receiver thread exiting for peer '{}'", m_peer_id);
// }

// void zmq_peer::process_message(const std::string &sender_id, ZmqMessageType type, const std::vector<uint8_t> &data)
// {
//     switch (type)
//     {
//     case ZmqMessageType::RAY_BATCH: {
//         try
//         {
//             auto rays = deserialize_rays(data);
//             m_rays_received.fetch_add(rays.size());
//             if (m_ray_callback)
//             {
//                 for (auto &ray : rays)
//                 {
//                     m_ray_callback(ray);
//                 }
//             }
//         }
//         catch (const std::exception &e)
//         {
//             spdlog::error("Failed to deserialize ray batch from '{}': {}", sender_id, e.what());
//         }
//         break;
//     }
//     case ZmqMessageType::TERMINATE: {
//         spdlog::info("Received termination signal from '{}'", sender_id);
//         if (m_terminate_callback)
//         {
//             m_terminate_callback();
//         }
//         break;
//     }
//     case ZmqMessageType::PEER_ANNOUNCE: {
//         std::string announced_id(data.begin(), data.end());
//         spdlog::info("Peer '{}' announced itself", announced_id);

//         // Track this peer if we don't know about it
//         {
//             std::lock_guard<std::mutex> lock(m_peers_mutex);
//             if (m_peers.find(announced_id) == m_peers.end())
//             {
//                 auto conn = std::make_shared<peer_connection>();
//                 conn->peer_id = announced_id;
//                 conn->connected = true; // They connected to us
//                 m_peers[announced_id] = conn;
//                 spdlog::info("Registered incoming peer '{}'", announced_id);
//             }
//             else
//             {
//                 m_peers[announced_id]->connected = true;
//             }
//         }
//         break;
//     }
//     default:
//         spdlog::warn("Unknown message type {} from '{}'", static_cast<int>(type), sender_id);
//         break;
//     }
// }

// void zmq_peer::sender_thread_fn()
// {
//     spdlog::debug("Sender thread started for peer '{}'", m_peer_id);

//     std::unordered_map<std::string, std::vector<models::cloud_ray>> batches;

//     while (m_running)
//     {
//         // Dequeue all pending rays
//         zmq_outbound_ray out;
//         size_t dequeued = 0;
//         while (m_outbound_queue.try_dequeue(out) && dequeued < ZMQ_BATCH_SIZE * 10)
//         {
//             batches[out.target_id].push_back(std::move(out.ray));
//             dequeued++;
//         }

//         // Send batches
//         for (auto &[target_id, rays] : batches)
//         {
//             if (rays.empty())
//                 continue;

//             // Split into smaller batches if needed
//             for (size_t i = 0; i < rays.size(); i += ZMQ_BATCH_SIZE)
//             {
//                 size_t end = std::min(i + ZMQ_BATCH_SIZE, rays.size());
//                 std::vector<models::cloud_ray> batch(rays.begin() + i, rays.begin() + end);

//                 auto data = serialize_rays(batch);

//                 if (target_id == models::WORKERS_ID)
//                 {
//                     // Broadcast to all workers (exclude master)
//                     broadcast_message(ZmqMessageType::RAY_BATCH, data, true);
//                     m_rays_sent.fetch_add(batch.size());
//                     m_batches_sent.fetch_add(1);
//                 }
//                 else
//                 {
//                     if (send_message(target_id, ZmqMessageType::RAY_BATCH, data))
//                     {
//                         m_rays_sent.fetch_add(batch.size());
//                         m_batches_sent.fetch_add(1);
//                     }
//                     else
//                     {
//                         m_send_failures.fetch_add(1);
//                         // Re-enqueue failed rays
//                         for (const auto &ray : batch)
//                         {
//                             zmq_outbound_ray requeue;
//                             requeue.ray = ray;
//                             requeue.target_id = target_id;
//                             m_outbound_queue.enqueue(requeue);
//                         }
//                     }
//                 }
//             }
//             rays.clear();
//         }

//         std::this_thread::sleep_for(m_flush_interval);
//     }

//     // Final flush on shutdown
//     spdlog::info("Performing final flush of queued rays...");
//     zmq_outbound_ray out;
//     while (m_outbound_queue.try_dequeue(out))
//     {
//         batches[out.target_id].push_back(std::move(out.ray));
//     }
//     for (auto &[target_id, rays] : batches)
//     {
//         if (!rays.empty())
//         {
//             auto data = serialize_rays(rays);
//             if (target_id == models::WORKERS_ID)
//             {
//                 broadcast_message(ZmqMessageType::RAY_BATCH, data, true);
//             }
//             else
//             {
//                 send_message(target_id, ZmqMessageType::RAY_BATCH, data);
//             }
//         }
//     }

//     spdlog::debug("Sender thread exiting for peer '{}'", m_peer_id);
// }

// bool zmq_peer::send_message(const std::string &target_id, ZmqMessageType type, const std::vector<uint8_t> &data)
// {
//     try
//     {
//         std::lock_guard<std::mutex> lock(m_socket_mutex);

//         // ROUTER socket message format: [identity, empty, type, data]
//         // The identity frame tells ZMQ which peer to route to

//         // Frame 1: Target identity
//         zmq::message_t identity_frame(target_id.data(), target_id.size());
//         m_socket.send(identity_frame, zmq::send_flags::sndmore);

//         // Frame 2: Empty delimiter (conventional for ROUTER)
//         zmq::message_t empty_frame(0);
//         m_socket.send(empty_frame, zmq::send_flags::sndmore);

//         // Frame 3: Message type
//         uint8_t type_byte = static_cast<uint8_t>(type);
//         zmq::message_t type_frame(&type_byte, 1);
//         m_socket.send(type_frame, zmq::send_flags::sndmore);

//         // Frame 4: Data
//         zmq::message_t data_frame(data.data(), data.size());
//         m_socket.send(data_frame, zmq::send_flags::none);

//         return true;
//     }
//     catch (const zmq::error_t &e)
//     {
//         spdlog::warn("Failed to send message to '{}': {}", target_id, e.what());
//         return false;
//     }
// }

// void zmq_peer::broadcast_message(ZmqMessageType type, const std::vector<uint8_t> &data, bool exclude_master)
// {
//     std::vector<std::string> targets;
//     {
//         std::lock_guard<std::mutex> lock(m_peers_mutex);
//         for (const auto &[peer_id, conn] : m_peers)
//         {
//             if (exclude_master && peer_id == models::MASTER_ID)
//                 continue;
//             if (conn->connected)
//             {
//                 targets.push_back(peer_id);
//             }
//         }
//     }

//     for (const auto &target_id : targets)
//     {
//         send_message(target_id, type, data);
//     }
// }

// void zmq_peer::enqueue_ray(const models::cloud_ray &ray, const std::string &target_id)
// {
//     zmq_outbound_ray out;
//     out.ray = ray;
//     out.target_id = target_id;
//     m_outbound_queue.enqueue(out);
//     m_rays_enqueued.fetch_add(1);
// }

// void zmq_peer::send_terminate_all()
// {
//     spdlog::info("Sending termination signal to all peers");
//     broadcast_message(ZmqMessageType::TERMINATE, {}, false);
// }

// void zmq_peer::set_ray_callback(std::function<void(models::cloud_ray &)> callback)
// {
//     m_ray_callback = std::move(callback);
// }

// void zmq_peer::set_terminate_callback(std::function<void()> callback)
// {
//     m_terminate_callback = std::move(callback);
// }

// bool zmq_peer::all_peers_connected() const
// {
//     std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(m_peers_mutex));
//     if (m_peers.empty())
//         return false;

//     for (const auto &[peer_id, conn] : m_peers)
//     {
//         if (!conn->connected.load())
//             return false;
//     }
//     return true;
// }

// bool zmq_peer::wait_for_peers(int timeout_seconds)
// {
//     if (m_expected_peers <= 0)
//     {
//         spdlog::warn("No expected peers set, skipping wait");
//         return true;
//     }

//     spdlog::info("Waiting for {} peers to connect (timeout: {}s)...", m_expected_peers, timeout_seconds);

//     int elapsed = 0;
//     while (m_running && elapsed < timeout_seconds)
//     {
//         size_t connected = 0;
//         {
//             std::lock_guard<std::mutex> lock(m_peers_mutex);
//             for (const auto &[peer_id, conn] : m_peers)
//             {
//                 if (conn->connected.load())
//                     connected++;
//             }
//         }

//         spdlog::info("Peer status: {}/{} connected", connected, m_expected_peers);

//         if (static_cast<int>(connected) >= m_expected_peers)
//         {
//             spdlog::info("All {} peers connected!", m_expected_peers);
//             return true;
//         }

//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         elapsed++;
//     }

//     spdlog::error("Timeout waiting for peers after {}s", timeout_seconds);
//     return false;
// }

// std::string zmq_peer::get_local_ip() const
// {
//     struct ifaddrs *ifaddr, *ifa;
//     if (getifaddrs(&ifaddr) == -1)
//     {
//         return "127.0.0.1";
//     }

//     std::string result = "127.0.0.1";
//     for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
//     {
//         if (ifa->ifa_addr == nullptr)
//             continue;

//         if (ifa->ifa_addr->sa_family == AF_INET)
//         {
//             // Skip loopback
//             if (ifa->ifa_flags & IFF_LOOPBACK)
//                 continue;

//             char ip[INET_ADDRSTRLEN];
//             struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
//             inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
//             result = ip;
//             break;
//         }
//     }

//     freeifaddrs(ifaddr);
//     return result;
// }

// zmq_peer::stats zmq_peer::get_stats() const
// {
//     return stats{m_rays_enqueued.load(), m_rays_sent.load(),     m_rays_received.load(),
//                  m_batches_sent.load(),  m_send_failures.load(), m_outbound_queue.size_approx()};
// }

// std::vector<uint8_t> zmq_peer::serialize_rays(const std::vector<models::cloud_ray> &rays)
// {
//     json j;
//     j["rays"] = rays;
//     std::string str = j.dump();
//     return std::vector<uint8_t>(str.begin(), str.end());
// }

// std::vector<models::cloud_ray> zmq_peer::deserialize_rays(const std::vector<uint8_t> &data)
// {
//     std::string str(data.begin(), data.end());
//     auto j = json::parse(str);
//     return j["rays"].get<std::vector<models::cloud_ray>>();
// }

// void zmq_peer::discover_peers(const std::string &namespace_name, const std::string &service_name)
// {
//     spdlog::info("Discovering peers in namespace '{}' service '{}'", namespace_name, service_name);

//     m_discovery_thread = std::thread([this, namespace_name, service_name]() {
//         Aws::Client::ClientConfiguration config;
//         std::string region = m_aws_region;
//         if (region.empty())
//         {
//             const char *env_region = std::getenv("AWS_REGION");
//             if (!env_region)
//                 env_region = std::getenv("AWS_DEFAULT_REGION");
//             if (env_region)
//                 region = env_region;
//         }
//         if (!region.empty())
//         {
//             config.region = region;
//             spdlog::info("Using region: {}", region);
//         }

//         Aws::ServiceDiscovery::ServiceDiscoveryClient client(config);

//         Aws::ServiceDiscovery::Model::ListInstancesRequest request;
//         request.SetServiceId(m_service_id);

//         int retries = 0;
//         while (m_running && retries < 60)
//         {
//             auto outcome = client.ListInstances(request);
//             if (outcome.IsSuccess())
//             {
//                 const auto &instances = outcome.GetResult().GetInstances();
//                 spdlog::info("Discovered {} instances", instances.size());

//                 for (const auto &instance : instances)
//                 {
//                     auto attrs = instance.GetAttributes();
//                     auto ip_it = attrs.find("AWS_INSTANCE_IPV4");
//                     auto port_it = attrs.find("AWS_INSTANCE_PORT");
//                     auto id_it = attrs.find("WORKER_ID");

//                     if (ip_it != attrs.end())
//                     {
//                         std::string peer_id = (id_it != attrs.end()) ? id_it->second : instance.GetId();
//                         std::string host = ip_it->second;
//                         uint16_t port = (port_it != attrs.end()) ? std::stoi(port_it->second) : DEFAULT_ZMQ_PORT;

//                         add_peer(peer_id, host, port);
//                     }
//                 }

//                 // Check if we have all expected peers
//                 size_t peer_count = 0;
//                 {
//                     std::lock_guard<std::mutex> lock(m_peers_mutex);
//                     peer_count = m_peers.size();
//                 }

//                 if (m_expected_peers > 0 && static_cast<int>(peer_count) >= m_expected_peers)
//                 {
//                     spdlog::info("Discovered all {} expected peers", m_expected_peers);
//                     return;
//                 }
//             }
//             else
//             {
//                 spdlog::warn("ListInstances failed: {}", outcome.GetError().GetMessage());
//             }

//             std::this_thread::sleep_for(std::chrono::seconds(1));
//             retries++;
//         }

//         spdlog::warn("Could not discover all peers after {} retries", retries);
//     });
// }

// void zmq_peer::register_instance(const std::string &namespace_name, const std::string &service_name)
// {
//     m_namespace_name = namespace_name;
//     m_service_name = service_name;
//     m_instance_id = m_peer_id;

//     if (m_service_id.empty())
//     {
//         spdlog::error("Cannot register instance: service_id is required");
//         return;
//     }

//     spdlog::info("Registering instance '{}' with Cloud Map service '{}'", m_peer_id, service_name);

//     try
//     {
//         Aws::ServiceDiscovery::ServiceDiscoveryClient client;

//         Aws::ServiceDiscovery::Model::RegisterInstanceRequest request;
//         request.SetServiceId(m_service_id);
//         request.SetInstanceId(m_instance_id);

//         Aws::Map<Aws::String, Aws::String> attributes;
//         attributes["AWS_INSTANCE_IPV4"] = get_local_ip();
//         attributes["AWS_INSTANCE_PORT"] = std::to_string(m_port);
//         attributes["WORKER_ID"] = m_peer_id;
//         request.SetAttributes(attributes);

//         auto outcome = client.RegisterInstance(request);
//         if (outcome.IsSuccess())
//         {
//             spdlog::info("Registered instance '{}' with IP {} port {}", m_peer_id, get_local_ip(), m_port);
//         }
//         else
//         {
//             spdlog::error("Failed to register instance: {}", outcome.GetError().GetMessage());
//         }
//     }
//     catch (const std::exception &e)
//     {
//         spdlog::error("Exception during Cloud Map registration: {}", e.what());
//     }
// }

// void zmq_peer::deregister_instance()
// {
//     if (m_instance_id.empty() || m_service_id.empty())
//     {
//         return;
//     }

//     spdlog::info("Deregistering instance '{}' from Cloud Map", m_instance_id);

//     try
//     {
//         Aws::ServiceDiscovery::ServiceDiscoveryClient client;

//         Aws::ServiceDiscovery::Model::DeregisterInstanceRequest request;
//         request.SetServiceId(m_service_id);
//         request.SetInstanceId(m_instance_id);

//         auto outcome = client.DeregisterInstance(request);
//         if (outcome.IsSuccess())
//         {
//             spdlog::info("Deregistered instance '{}'", m_instance_id);
//         }
//         else
//         {
//             spdlog::warn("Failed to deregister instance: {}", outcome.GetError().GetMessage());
//         }
//     }
//     catch (const std::exception &e)
//     {
//         spdlog::warn("Exception during Cloud Map deregistration: {}", e.what());
//     }
// }

// } // namespace cloud
