#pragma once

#include "models/cloud_ray.hpp"
#include "models/pixel.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cloud
{

constexpr uint16_t DEFAULT_TCP_PORT = 9000;
constexpr size_t TCP_BATCH_SIZE = 2000;               // Smaller batches for reliability
constexpr size_t PIXEL_BATCH_SIZE = 2500;             // ~85B/pixel JSON -> ~212KB
constexpr size_t MESSAGE_HEADER_SIZE = 8;             // 4 bytes for type + 4 bytes for length
constexpr size_t MAX_MESSAGE_SIZE = 50 * 1024 * 1024; // 50MB max message size

enum class MessageType : uint32_t
{
    RAY_BATCH = 1,
    TERMINATE = 2,
    HANDSHAKE = 3,
    PIXEL_BATCH = 4
};

struct peer_info
{
    std::string worker_id;
    std::string host;
    uint16_t port;
};

// Outbound ray with target
struct outbound_ray
{
    models::cloud_ray ray;
    std::string target_id;
};

// Outbound pixel with target (master -> web)
struct outbound_pixel
{
    models::pixel pixel;
    std::string target_id;
};

class tcp_peer
{
  public:
    tcp_peer(const std::string &worker_id, uint16_t port = DEFAULT_TCP_PORT);
    ~tcp_peer();

    // Start the server and discovery
    void start(const std::string &namespace_name, const std::string &service_name, const std::string &service_id = "",
               int expected_peers = 0, const std::string &aws_region = "");
    void stop();

    // Wait until all expected peers are discovered and connected (with timeout)
    bool wait_for_peers(int timeout_seconds = 120);

    // Enqueue a single ray for sending (lock-free)
    void enqueue_ray(const models::cloud_ray &ray, const std::string &target_id);

    // Enqueue a single pixel for sending (lock-free) - used by master to push
    // accumulated pixels to the web backend.
    void enqueue_pixel(const models::pixel &pixel, const std::string &target_id);

    // Register a peer with a known host/port without going through Cloud Map.
    // Used by the master to dial the web backend, which is not part of the
    // discovery namespace.
    void register_peer(const std::string &worker_id, const std::string &host, uint16_t port);

    // Send termination signal to all workers
    void send_terminate_all();

    // Set callback for received rays
    void set_ray_callback(std::function<void(models::cloud_ray &)> callback);

    // Set callback for termination signal
    void set_terminate_callback(std::function<void()> callback);

    // Check if all peers are connected
    bool all_peers_connected() const;

    // Get local IP address
    std::string get_local_ip() const;

    // Statistics
    struct stats
    {
        uint64_t rays_enqueued;
        uint64_t rays_sent;
        uint64_t rays_received;
        uint64_t batches_sent;
        uint64_t send_failures;
        uint64_t reconnections;
        uint64_t pending_rays;
    };
    stats get_stats() const;

  private:
    // Connection management
    void accept_connections();
    void handle_connection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void reader_thread_fn(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    bool read_exact(boost::asio::ip::tcp::socket &socket, uint8_t *buffer, size_t size);
    void process_message(const std::vector<uint8_t> &data, MessageType type);

    // Peer management
    void add_peer(const peer_info &peer);
    void connect_to_peer(const peer_info &peer);
    void schedule_reconnect(const std::string &worker_id, int delay_ms);
    void reconnect_peer(const std::string &worker_id);

    // Cloud Map
    void discover_peers(const std::string &namespace_name, const std::string &service_name);
    void register_instance(const std::string &namespace_name, const std::string &service_name);
    void deregister_instance();

    // Sending - synchronous and protected by mutex
    void flush_thread_fn();
    void pixel_flush_thread_fn();
    void send_batch_to_peer(const std::string &target_id, std::vector<models::cloud_ray> &rays);
    void send_pixel_batch_to_peer(const std::string &target_id, std::vector<models::pixel> &pixels);

    // Binary serialization (more efficient than JSON)
    std::vector<uint8_t> serialize_rays_binary(const std::vector<models::cloud_ray> &rays);
    std::vector<models::cloud_ray> deserialize_rays_binary(const std::vector<uint8_t> &data);
    std::vector<uint8_t> serialize_pixels(const std::vector<models::pixel> &pixels);

    // Peer connection structure with write mutex for thread safety
    struct peer_connection
    {
        std::string worker_id;
        std::string host;
        uint16_t port;
        std::shared_ptr<boost::asio::ip::tcp::socket> socket;
        std::mutex write_mutex; // Protects socket writes
        std::atomic<bool> connected{false};
        std::atomic<bool> write_in_progress{false};
        std::atomic<int> retry_count{0};
        std::atomic<int> consecutive_failures{0};
    };

    bool send_raw_message_sync(peer_connection &conn, MessageType type, const std::vector<uint8_t> &data);

  private:
    std::string m_worker_id;
    uint16_t m_port;
    std::atomic<bool> m_running{false};

    // Asio
    boost::asio::io_context m_io_context;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    std::thread m_acceptor_thread;

    // Lock-free outbound queue
    moodycamel::ConcurrentQueue<outbound_ray> m_outbound_queue;
    moodycamel::ConcurrentQueue<outbound_pixel> m_outbound_pixel_queue;

    // Peer connections
    std::vector<std::shared_ptr<peer_connection>> m_peers;
    std::mutex m_peers_mutex;
    std::atomic<size_t> m_peer_count{0};
    int m_expected_peers{0};

    // Reader threads (one per inbound connection)
    std::vector<std::thread> m_reader_threads;
    std::mutex m_reader_threads_mutex;

    // Callbacks
    std::function<void(models::cloud_ray &)> m_ray_callback;
    std::function<void()> m_terminate_callback;

    // Flush thread
    std::thread m_flush_thread;
    std::thread m_pixel_flush_thread;
    std::chrono::milliseconds m_flush_interval{25};        // Faster flush for responsiveness
    std::chrono::milliseconds m_pixel_flush_interval{100}; // Pixels can be batched a bit longer

    // Cloud Map
    std::string m_service_id;
    std::string m_instance_id;
    std::string m_namespace_name;
    std::string m_service_name;
    std::string m_aws_region;
    std::thread m_discovery_thread;

    // Constants
    static constexpr int MAX_CONNECT_RETRIES = 60;
    static constexpr int BASE_RETRY_DELAY_MS = 100;
    static constexpr int MAX_RETRY_DELAY_MS = 5000;
    static constexpr int MAX_CONSECUTIVE_FAILURES = 3;
    static constexpr int SOCKET_TIMEOUT_SECONDS = 30;

    // Statistics
    std::atomic<uint64_t> m_rays_enqueued{0};
    std::atomic<uint64_t> m_rays_sent{0};
    std::atomic<uint64_t> m_rays_received{0};
    std::atomic<uint64_t> m_batches_sent{0};
    std::atomic<uint64_t> m_send_failures{0};
    std::atomic<uint64_t> m_reconnections{0};
};

} // namespace cloud
