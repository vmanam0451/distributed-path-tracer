// #pragma once

// #include "models/cloud_ray.hpp"
// #include <atomic>
// #include <concurrentqueue/concurrentqueue.h>
// #include <functional>
// #include <memory>
// #include <mutex>
// #include <string>
// #include <thread>
// #include <unordered_map>
// #include <vector>
// #include <zmq.hpp>

// namespace cloud
// {

// constexpr uint16_t DEFAULT_ZMQ_PORT = 9000;
// constexpr size_t ZMQ_BATCH_SIZE = 2000;

// // Message types sent as first frame
// enum class ZmqMessageType : uint8_t
// {
//     RAY_BATCH = 1,
//     TERMINATE = 2,
//     PEER_ANNOUNCE = 3
// };

// struct zmq_peer_info
// {
//     std::string peer_id;
//     std::string endpoint; // e.g., "tcp://192.168.1.5:9000"
// };

// // Outbound ray with target
// struct zmq_outbound_ray
// {
//     models::cloud_ray ray;
//     std::string target_id;
// };

// /**
//  * ZeroMQ-based peer for distributed path tracing.
//  *
//  * Uses ROUTER sockets for full peer-to-peer mesh communication.
//  * Any node can send to any other node directly.
//  *
//  * Architecture:
//  * - Each peer binds a ROUTER socket on its port
//  * - Each peer connects to other peers' ROUTER sockets
//  * - Messages are routed by peer ID (ZMQ identity)
//  *
//  * Example topology (2 workers + 1 master):
//  *   Worker1 <---> Master <---> Worker2
//  *      ^                          ^
//  *      |__________________________|
//  *        (direct P2P connection)
//  */
// class zmq_peer
// {
//   public:
//     zmq_peer(const std::string &peer_id, uint16_t port = DEFAULT_ZMQ_PORT);
//     ~zmq_peer();

//     // Start the peer - binds socket and optionally discovers peers via Cloud Map
//     void start(const std::string &namespace_name = "", const std::string &service_name = "",
//                const std::string &service_id = "", int expected_peers = 0, const std::string &aws_region = "");
//     void stop();

//     // Manually add a peer to connect to (alternative to Cloud Map discovery)
//     void add_peer(const std::string &peer_id, const std::string &host, uint16_t port);

//     // Wait until all expected peers are connected (with timeout)
//     bool wait_for_peers(int timeout_seconds = 120);

//     // Enqueue a ray for sending to a specific peer
//     // Use target_id = "WORKERS" to broadcast to all workers (excluding MASTER)
//     void enqueue_ray(const models::cloud_ray &ray, const std::string &target_id);

//     // Send termination signal to all connected peers
//     void send_terminate_all();

//     // Set callback for received rays
//     void set_ray_callback(std::function<void(models::cloud_ray &)> callback);

//     // Set callback for termination signal
//     void set_terminate_callback(std::function<void()> callback);

//     // Check if all expected peers are connected
//     bool all_peers_connected() const;

//     // Get local IP address
//     std::string get_local_ip() const;

//     // Statistics
//     struct stats
//     {
//         uint64_t rays_enqueued;
//         uint64_t rays_sent;
//         uint64_t rays_received;
//         uint64_t batches_sent;
//         uint64_t send_failures;
//         uint64_t pending_rays;
//     };
//     stats get_stats() const;

//   private:
//     // Internal peer connection tracking
//     struct peer_connection
//     {
//         std::string peer_id;
//         std::string endpoint;
//         std::atomic<bool> connected{false};
//     };

//     // Receiver thread - polls for incoming messages
//     void receiver_thread_fn();

//     // Sender thread - batches and sends outgoing rays
//     void sender_thread_fn();

//     // Process received message
//     void process_message(const std::string &sender_id, ZmqMessageType type, const std::vector<uint8_t> &data);

//     // Send a message to a specific peer
//     bool send_message(const std::string &target_id, ZmqMessageType type, const std::vector<uint8_t> &data);

//     // Send to all connected peers
//     void broadcast_message(ZmqMessageType type, const std::vector<uint8_t> &data, bool exclude_master = false);

//     // Serialization
//     std::vector<uint8_t> serialize_rays(const std::vector<models::cloud_ray> &rays);
//     std::vector<models::cloud_ray> deserialize_rays(const std::vector<uint8_t> &data);

//     // Cloud Map integration
//     void discover_peers(const std::string &namespace_name, const std::string &service_name);
//     void register_instance(const std::string &namespace_name, const std::string &service_name);
//     void deregister_instance();

//     // Connect to a peer's endpoint
//     void connect_to_peer(const std::string &peer_id, const std::string &endpoint);

//   private:
//     std::string m_peer_id;
//     uint16_t m_port;
//     std::atomic<bool> m_running{false};

//     // ZeroMQ context and socket
//     zmq::context_t m_context;
//     zmq::socket_t m_socket; // ROUTER socket for P2P communication
//     std::mutex m_socket_mutex;

//     // Peer connections
//     std::unordered_map<std::string, std::shared_ptr<peer_connection>> m_peers;
//     std::mutex m_peers_mutex;
//     int m_expected_peers{0};

//     // Lock-free outbound queue
//     moodycamel::ConcurrentQueue<zmq_outbound_ray> m_outbound_queue;

//     // Threads
//     std::thread m_receiver_thread;
//     std::thread m_sender_thread;
//     std::thread m_discovery_thread;

//     // Callbacks
//     std::function<void(models::cloud_ray &)> m_ray_callback;
//     std::function<void()> m_terminate_callback;

//     // Sender settings
//     std::chrono::milliseconds m_flush_interval{10};

//     // Cloud Map
//     std::string m_service_id;
//     std::string m_instance_id;
//     std::string m_namespace_name;
//     std::string m_service_name;
//     std::string m_aws_region;

//     // Statistics
//     std::atomic<uint64_t> m_rays_enqueued{0};
//     std::atomic<uint64_t> m_rays_sent{0};
//     std::atomic<uint64_t> m_rays_received{0};
//     std::atomic<uint64_t> m_batches_sent{0};
//     std::atomic<uint64_t> m_send_failures{0};
// };

// } // namespace cloud
