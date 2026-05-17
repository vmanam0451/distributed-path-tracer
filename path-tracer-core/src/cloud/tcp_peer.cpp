#include "tcp_peer.hpp"
#include <aws/core/Aws.h>
#include <aws/servicediscovery/ServiceDiscoveryClient.h>
#include <aws/servicediscovery/model/DeregisterInstanceRequest.h>
#include <aws/servicediscovery/model/DiscoverInstancesRequest.h>
#include <aws/servicediscovery/model/ListInstancesRequest.h>
#include <aws/servicediscovery/model/RegisterInstanceRequest.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

using boost::asio::ip::tcp;

namespace cloud
{

tcp_peer::tcp_peer(const std::string &worker_id, uint16_t port) : m_worker_id(worker_id), m_port(port)
{
}

tcp_peer::~tcp_peer()
{
    stop();
}

void tcp_peer::start(const std::string &namespace_name, const std::string &service_name, const std::string &service_id,
                     int expected_peers, const std::string &aws_region)
{
    m_running = true;
    m_service_id = service_id;
    m_expected_peers = expected_peers;
    m_aws_region = aws_region;

    // Create acceptor with SO_REUSEADDR
    tcp::endpoint endpoint(tcp::v4(), m_port);
    m_acceptor = std::make_unique<tcp::acceptor>(m_io_context, endpoint);
    m_acceptor->set_option(boost::asio::socket_base::reuse_address(true));

    spdlog::info("TCP peer started on port {} for worker {}", m_port, m_worker_id);

    // Start acceptor thread
    m_acceptor_thread = std::thread([this]() {
        while (m_running)
        {
            try
            {
                accept_connections();
            }
            catch (const std::exception &e)
            {
                if (m_running)
                {
                    spdlog::error("Acceptor exception: {}", e.what());
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    });

    // Start flush threads (rays + pixels share the transport but use separate queues)
    m_flush_thread = std::thread(&tcp_peer::flush_thread_fn, this);
    m_pixel_flush_thread = std::thread(&tcp_peer::pixel_flush_thread_fn, this);

    // Register with Cloud Map and discover peers
    if (!namespace_name.empty() && !service_name.empty())
    {
        register_instance(namespace_name, service_name);
        discover_peers(namespace_name, service_name);
    }
}

void tcp_peer::stop()
{
    if (!m_running.exchange(false))
        return;

    spdlog::info("Stopping TCP peer for worker {}", m_worker_id);

    // Wait for discovery thread
    if (m_discovery_thread.joinable())
    {
        m_discovery_thread.join();
    }

    // Deregister from Cloud Map
    deregister_instance();

    // Close acceptor
    if (m_acceptor)
    {
        boost::system::error_code ec;
        m_acceptor->close(ec);
    }

    // Close all peer connections
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (auto &peer : m_peers)
        {
            if (peer->socket && peer->socket->is_open())
            {
                boost::system::error_code ec;
                peer->socket->shutdown(tcp::socket::shutdown_both, ec);
                peer->socket->close(ec);
            }
            peer->connected = false;
        }
    }

    if (m_acceptor_thread.joinable())
    {
        m_acceptor_thread.join();
    }

    if (m_flush_thread.joinable())
    {
        m_flush_thread.join();
    }

    if (m_pixel_flush_thread.joinable())
    {
        m_pixel_flush_thread.join();
    }

    // Wait for reader threads
    {
        std::lock_guard<std::mutex> lock(m_reader_threads_mutex);
        for (auto &thread : m_reader_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        m_reader_threads.clear();
    }

    spdlog::info("TCP peer stopped for worker {}", m_worker_id);
}

void tcp_peer::accept_connections()
{
    if (!m_running || !m_acceptor)
        return;

    auto socket = std::make_shared<tcp::socket>(m_io_context);
    boost::system::error_code ec;

    m_acceptor->accept(*socket, ec);

    if (ec)
    {
        if (m_running && ec != boost::asio::error::operation_aborted)
        {
            spdlog::error("Accept error: {}", ec.message());
        }
        return;
    }

    if (m_running)
    {
        std::string remote = socket->remote_endpoint().address().to_string();
        spdlog::info("Accepted connection from {}", remote);
        handle_connection(socket);
    }
}

void tcp_peer::handle_connection(std::shared_ptr<tcp::socket> socket)
{
    // Configure socket for reliability
    socket->set_option(tcp::no_delay(true));

    // Set socket timeouts
    struct timeval tv;
    tv.tv_sec = SOCKET_TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Enable keep-alive
    socket->set_option(boost::asio::socket_base::keep_alive(true));

    // Start reader thread for this socket
    std::lock_guard<std::mutex> lock(m_reader_threads_mutex);
    m_reader_threads.emplace_back(&tcp_peer::reader_thread_fn, this, socket);
}

void tcp_peer::reader_thread_fn(std::shared_ptr<tcp::socket> socket)
{
    std::array<uint8_t, MESSAGE_HEADER_SIZE> header;

    while (m_running && socket->is_open())
    {
        // Read header synchronously
        if (!read_exact(*socket, header.data(), MESSAGE_HEADER_SIZE))
        {
            break;
        }

        // Parse header (big-endian)
        uint32_t msg_type = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        uint32_t msg_len = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];

        // Validate message size
        if (msg_len > MAX_MESSAGE_SIZE)
        {
            spdlog::error("Message too large: {} bytes (max: {}). Possible stream corruption.", msg_len,
                          MAX_MESSAGE_SIZE);
            // Don't continue - stream is likely corrupted
            break;
        }

        // Read body
        std::vector<uint8_t> body(msg_len);
        if (msg_len > 0 && !read_exact(*socket, body.data(), msg_len))
        {
            break;
        }

        // Process message
        process_message(body, static_cast<MessageType>(msg_type));
    }

    spdlog::debug("Reader thread exiting for socket");
}

bool tcp_peer::read_exact(tcp::socket &socket, uint8_t *buffer, size_t size)
{
    size_t total_read = 0;
    while (total_read < size && m_running)
    {
        boost::system::error_code ec;
        size_t bytes = socket.read_some(boost::asio::buffer(buffer + total_read, size - total_read), ec);

        if (ec)
        {
            if (ec == boost::asio::error::eof)
            {
                spdlog::debug("Connection closed by peer");
            }
            else if (ec != boost::asio::error::operation_aborted && m_running)
            {
                spdlog::warn("Read error: {}", ec.message());
            }
            return false;
        }

        if (bytes == 0)
        {
            return false;
        }

        total_read += bytes;
    }
    return total_read == size;
}

void tcp_peer::process_message(const std::vector<uint8_t> &data, MessageType type)
{
    switch (type)
    {
    case MessageType::RAY_BATCH: {
        try
        {
            auto rays = deserialize_rays_binary(data);
            m_rays_received.fetch_add(rays.size());
            if (m_ray_callback)
            {
                for (auto &ray : rays)
                {
                    m_ray_callback(ray);
                }
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to deserialize ray batch: {}", e.what());
        }
        break;
    }
    case MessageType::TERMINATE: {
        spdlog::info("Received termination signal via TCP");
        if (m_terminate_callback)
        {
            m_terminate_callback();
        }
        break;
    }
    case MessageType::HANDSHAKE: {
        std::string peer_id(data.begin(), data.end());
        spdlog::info("Received handshake from worker {}", peer_id);
        break;
    }
    case MessageType::PIXEL_BATCH: {
        // Pixels flow from master -> web only; C++ peers don't consume them.
        spdlog::debug("Ignoring inbound PIXEL_BATCH ({} bytes)", data.size());
        break;
    }
    default:
        spdlog::warn("Unknown message type: {}", static_cast<uint32_t>(type));
        break;
    }
}

void tcp_peer::add_peer(const peer_info &peer)
{
    if (peer.worker_id == m_worker_id)
        return;

    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);

        // Check if already exists
        for (const auto &p : m_peers)
        {
            if (p->worker_id == peer.worker_id)
                return;
        }

        // Add new peer
        auto conn = std::make_shared<peer_connection>();
        conn->worker_id = peer.worker_id;
        conn->host = peer.host;
        conn->port = peer.port;
        conn->connected = false;
        conn->retry_count = 0;
        conn->consecutive_failures = 0;

        m_peers.push_back(conn);
        m_peer_count.store(m_peers.size());
    }

    spdlog::info("Added peer: {} at {}:{}", peer.worker_id, peer.host, peer.port);
    connect_to_peer(peer);
}

void tcp_peer::connect_to_peer(const peer_info &peer)
{
    // Find the peer connection
    std::shared_ptr<peer_connection> conn;
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (auto &p : m_peers)
        {
            if (p->worker_id == peer.worker_id)
            {
                conn = p;
                break;
            }
        }
    }

    if (!conn)
        return;

    int retry = conn->retry_count.fetch_add(1);
    if (retry >= MAX_CONNECT_RETRIES)
    {
        spdlog::error("Max retries ({}) reached for peer {}", MAX_CONNECT_RETRIES, peer.worker_id);
        return;
    }

    try
    {
        auto socket = std::make_shared<tcp::socket>(m_io_context);
        tcp::resolver resolver(m_io_context);
        auto endpoints = resolver.resolve(peer.host, std::to_string(peer.port));

        boost::system::error_code ec;
        boost::asio::connect(*socket, endpoints, ec);

        if (!ec)
        {
            // Configure socket
            socket->set_option(tcp::no_delay(true));
            socket->set_option(boost::asio::socket_base::keep_alive(true));

            // Set send timeout
            struct timeval tv;
            tv.tv_sec = SOCKET_TIMEOUT_SECONDS;
            tv.tv_usec = 0;
            setsockopt(socket->native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            setsockopt(socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            {
                std::lock_guard<std::mutex> write_lock(conn->write_mutex);
                conn->socket = socket;
                conn->connected = true;
                conn->retry_count = 0;
                conn->consecutive_failures = 0;
            }

            spdlog::info("Connected to peer {} at {}:{}", peer.worker_id, peer.host, peer.port);

            // Send handshake
            std::vector<uint8_t> id_data(m_worker_id.begin(), m_worker_id.end());
            send_raw_message_sync(*conn, MessageType::HANDSHAKE, id_data);

            // Start reader thread for responses
            {
                std::lock_guard<std::mutex> lock(m_reader_threads_mutex);
                m_reader_threads.emplace_back(&tcp_peer::reader_thread_fn, this, socket);
            }
        }
        else
        {
            int delay_ms = std::min(BASE_RETRY_DELAY_MS * (1 << std::min(retry, 10)), MAX_RETRY_DELAY_MS);
            spdlog::warn("Failed to connect to peer {} (attempt {}/{}): {}, retrying in {}ms", peer.worker_id,
                         retry + 1, MAX_CONNECT_RETRIES, ec.message(), delay_ms);
            schedule_reconnect(peer.worker_id, delay_ms);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception connecting to peer {}: {}", peer.worker_id, e.what());
        int delay_ms = std::min(BASE_RETRY_DELAY_MS * (1 << std::min(retry, 10)), MAX_RETRY_DELAY_MS);
        schedule_reconnect(peer.worker_id, delay_ms);
    }
}

void tcp_peer::schedule_reconnect(const std::string &worker_id, int delay_ms)
{
    std::thread([this, worker_id, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        if (m_running)
        {
            reconnect_peer(worker_id);
        }
    }).detach();
}

void tcp_peer::reconnect_peer(const std::string &worker_id)
{
    std::shared_ptr<peer_connection> conn;
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (auto &p : m_peers)
        {
            if (p->worker_id == worker_id)
            {
                conn = p;
                break;
            }
        }
    }

    if (!conn)
        return;

    m_reconnections.fetch_add(1);
    peer_info peer{conn->worker_id, conn->host, conn->port};
    connect_to_peer(peer);
}

void tcp_peer::enqueue_ray(const models::cloud_ray &ray, const std::string &target_id)
{
    // Handle broadcast
    if (target_id == "WORKERS")
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (const auto &peer : m_peers)
        {
            if (peer->worker_id == models::MASTER_ID || peer->worker_id == models::WEB_ID)
                continue;

            outbound_ray out;
            out.ray = ray;
            out.target_id = peer->worker_id;
            m_outbound_queue.enqueue(out);
            m_rays_enqueued.fetch_add(1);
        }
    }
    else
    {
        outbound_ray out;
        out.ray = ray;
        out.target_id = target_id;
        m_outbound_queue.enqueue(out);
        m_rays_enqueued.fetch_add(1);
    }
}

void tcp_peer::enqueue_pixel(const models::pixel &pixel, const std::string &target_id)
{
    outbound_pixel out;
    out.pixel = pixel;
    out.target_id = target_id;
    m_outbound_pixel_queue.enqueue(out);
}

void tcp_peer::register_peer(const std::string &worker_id, const std::string &host, uint16_t port)
{
    peer_info peer{worker_id, host, port};
    add_peer(peer);
}

void tcp_peer::flush_thread_fn()
{
    std::unordered_map<std::string, std::vector<models::cloud_ray>> batches;

    while (m_running)
    {
        // Dequeue all pending rays
        outbound_ray out;
        size_t dequeued = 0;
        while (m_outbound_queue.try_dequeue(out) && dequeued < TCP_BATCH_SIZE * 10)
        {
            batches[out.target_id].push_back(std::move(out.ray));
            dequeued++;
        }

        // Send batches
        for (auto &[target_id, rays] : batches)
        {
            if (!rays.empty())
            {
                // Split into smaller batches if needed
                for (size_t i = 0; i < rays.size(); i += TCP_BATCH_SIZE)
                {
                    size_t end = std::min(i + TCP_BATCH_SIZE, rays.size());
                    std::vector<models::cloud_ray> batch(rays.begin() + i, rays.begin() + end);
                    send_batch_to_peer(target_id, batch);
                }
                rays.clear();
            }
        }

        std::this_thread::sleep_for(m_flush_interval);
    }
}

void tcp_peer::pixel_flush_thread_fn()
{
    std::unordered_map<std::string, std::vector<models::pixel>> batches;

    while (m_running)
    {
        outbound_pixel out;
        size_t dequeued = 0;
        while (m_outbound_pixel_queue.try_dequeue(out) && dequeued < PIXEL_BATCH_SIZE * 4)
        {
            batches[out.target_id].push_back(std::move(out.pixel));
            dequeued++;
        }

        for (auto &[target_id, pixels] : batches)
        {
            if (pixels.empty())
                continue;

            for (size_t i = 0; i < pixels.size(); i += PIXEL_BATCH_SIZE)
            {
                size_t end = std::min(i + PIXEL_BATCH_SIZE, pixels.size());
                std::vector<models::pixel> batch(pixels.begin() + i, pixels.begin() + end);
                send_pixel_batch_to_peer(target_id, batch);
            }
            pixels.clear();
        }

        std::this_thread::sleep_for(m_pixel_flush_interval);
    }

    // Final drain on shutdown — best effort
    {
        outbound_pixel out;
        while (m_outbound_pixel_queue.try_dequeue(out))
        {
            batches[out.target_id].push_back(std::move(out.pixel));
        }
        for (auto &[target_id, pixels] : batches)
        {
            if (pixels.empty())
                continue;
            for (size_t i = 0; i < pixels.size(); i += PIXEL_BATCH_SIZE)
            {
                size_t end = std::min(i + PIXEL_BATCH_SIZE, pixels.size());
                std::vector<models::pixel> batch(pixels.begin() + i, pixels.begin() + end);
                send_pixel_batch_to_peer(target_id, batch);
            }
            pixels.clear();
        }
    }
}

void tcp_peer::send_pixel_batch_to_peer(const std::string &target_id, std::vector<models::pixel> &pixels)
{
    if (pixels.empty())
        return;

    std::shared_ptr<peer_connection> conn;
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (auto &p : m_peers)
        {
            if (p->worker_id == target_id)
            {
                conn = p;
                break;
            }
        }
    }

    if (!conn)
    {
        spdlog::warn("Pixel target peer {} not found, re-queueing {} pixels", target_id, pixels.size());
        for (const auto &px : pixels)
        {
            outbound_pixel out;
            out.pixel = px;
            out.target_id = target_id;
            m_outbound_pixel_queue.enqueue(out);
        }
        return;
    }

    if (!conn->connected.load())
    {
        for (const auto &px : pixels)
        {
            outbound_pixel out;
            out.pixel = px;
            out.target_id = target_id;
            m_outbound_pixel_queue.enqueue(out);
        }

        if (conn->retry_count.load() == 0 || conn->retry_count.load() >= MAX_CONNECT_RETRIES)
        {
            conn->retry_count = 0;
            spdlog::warn("Pixel peer {} disconnected, triggering reconnect ({} pixels re-queued)", target_id,
                         pixels.size());
            schedule_reconnect(target_id, BASE_RETRY_DELAY_MS);
        }
        return;
    }

    auto data = serialize_pixels(pixels);
    bool success = send_raw_message_sync(*conn, MessageType::PIXEL_BATCH, data);

    if (success)
    {
        m_batches_sent.fetch_add(1);
        conn->consecutive_failures = 0;
    }
    else
    {
        m_send_failures.fetch_add(1);
        int failures = conn->consecutive_failures.fetch_add(1) + 1;

        spdlog::warn("Failed to send pixel batch to {}, re-queueing {} pixels (consecutive failures: {})", target_id,
                     pixels.size(), failures);

        for (const auto &px : pixels)
        {
            outbound_pixel out;
            out.pixel = px;
            out.target_id = target_id;
            m_outbound_pixel_queue.enqueue(out);
        }

        if (failures >= MAX_CONSECUTIVE_FAILURES)
        {
            spdlog::warn("Too many consecutive pixel failures for {}, triggering reconnect", target_id);
            conn->connected = false;
            schedule_reconnect(target_id, BASE_RETRY_DELAY_MS);
        }
    }
}

void tcp_peer::send_batch_to_peer(const std::string &target_id, std::vector<models::cloud_ray> &rays)
{
    if (rays.empty())
        return;

    // Find peer
    std::shared_ptr<peer_connection> conn;
    {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        for (auto &p : m_peers)
        {
            if (p->worker_id == target_id)
            {
                conn = p;
                break;
            }
        }
    }

    if (!conn)
    {
        spdlog::warn("Peer {} not found, re-queueing {} rays", target_id, rays.size());
        for (const auto &ray : rays)
        {
            outbound_ray out;
            out.ray = ray;
            out.target_id = target_id;
            m_outbound_queue.enqueue(out);
        }
        return;
    }

    if (!conn->connected.load())
    {
        // Re-enqueue rays - peer not connected yet
        for (const auto &ray : rays)
        {
            outbound_ray out;
            out.ray = ray;
            out.target_id = target_id;
            m_outbound_queue.enqueue(out);
        }

        // Trigger reconnection if not already in progress
        if (conn->retry_count.load() == 0 || conn->retry_count.load() >= MAX_CONNECT_RETRIES)
        {
            conn->retry_count = 0; // Reset for fresh reconnection attempt
            spdlog::warn("Peer {} disconnected, triggering reconnect ({} rays re-queued)", target_id, rays.size());
            schedule_reconnect(target_id, BASE_RETRY_DELAY_MS);
        }
        return;
    }

    // Serialize with binary format
    auto data = serialize_rays_binary(rays);

    // Send with retry logicpath-tracer-core/src/cloud/tcp_peer.cpp
    bool success = send_raw_message_sync(*conn, MessageType::RAY_BATCH, data);

    if (success)
    {
        m_rays_sent.fetch_add(rays.size());
        m_batches_sent.fetch_add(1);
        conn->consecutive_failures = 0;
    }
    else
    {
        m_send_failures.fetch_add(1);
        int failures = conn->consecutive_failures.fetch_add(1) + 1;

        spdlog::warn("Failed to send batch to {}, re-queueing {} rays (consecutive failures: {})", target_id,
                     rays.size(), failures);

        // Re-enqueue rays for retry
        for (const auto &ray : rays)
        {
            outbound_ray out;
            out.ray = ray;
            out.target_id = target_id;
            m_outbound_queue.enqueue(out);
        }

        // Trigger reconnection if too many failures
        if (failures >= MAX_CONSECUTIVE_FAILURES)
        {
            spdlog::warn("Too many consecutive failures for {}, triggering reconnect", target_id);
            conn->connected = false;
            schedule_reconnect(target_id, BASE_RETRY_DELAY_MS);
        }
    }
}

bool tcp_peer::send_raw_message_sync(peer_connection &conn, MessageType type, const std::vector<uint8_t> &data)
{
    std::lock_guard<std::mutex> lock(conn.write_mutex);

    if (!conn.socket || !conn.socket->is_open())
    {
        return false;
    }

    // Build message with header
    std::vector<uint8_t> message(MESSAGE_HEADER_SIZE + data.size());

    // Message type (4 bytes big-endian)
    uint32_t msg_type = static_cast<uint32_t>(type);
    message[0] = (msg_type >> 24) & 0xFF;
    message[1] = (msg_type >> 16) & 0xFF;
    message[2] = (msg_type >> 8) & 0xFF;
    message[3] = msg_type & 0xFF;

    // Message length (4 bytes big-endian)
    uint32_t msg_len = static_cast<uint32_t>(data.size());
    message[4] = (msg_len >> 24) & 0xFF;
    message[5] = (msg_len >> 16) & 0xFF;
    message[6] = (msg_len >> 8) & 0xFF;
    message[7] = msg_len & 0xFF;

    // Copy data
    std::copy(data.begin(), data.end(), message.begin() + MESSAGE_HEADER_SIZE);

    // Synchronous write with error handling
    boost::system::error_code ec;
    size_t total_written = 0;
    size_t total_size = message.size();

    while (total_written < total_size)
    {
        size_t written = conn.socket->write_some(
            boost::asio::buffer(message.data() + total_written, total_size - total_written), ec);

        if (ec)
        {
            spdlog::error("Write error to {}: {}", conn.worker_id, ec.message());
            conn.connected = false;
            return false;
        }

        total_written += written;
    }

    return true;
}

void tcp_peer::send_terminate_all()
{
    spdlog::info("Sending termination signal to all peers");

    std::lock_guard<std::mutex> lock(m_peers_mutex);
    for (auto &conn : m_peers)
    {
        if (conn->connected.load())
        {
            // Try multiple times to ensure delivery
            for (int attempt = 0; attempt < 3; attempt++)
            {
                if (send_raw_message_sync(*conn, MessageType::TERMINATE, {}))
                {
                    spdlog::info("Sent termination to {}", conn->worker_id);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

void tcp_peer::set_ray_callback(std::function<void(models::cloud_ray &)> callback)
{
    m_ray_callback = std::move(callback);
}

void tcp_peer::set_terminate_callback(std::function<void()> callback)
{
    m_terminate_callback = std::move(callback);
}

bool tcp_peer::all_peers_connected() const
{
    size_t count = m_peer_count.load();
    if (count == 0)
        return false;

    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(m_peers_mutex));
    for (const auto &peer : m_peers)
    {
        if (!peer->connected.load())
            return false;
    }
    return true;
}

bool tcp_peer::wait_for_peers(int timeout_seconds)
{
    if (m_expected_peers <= 0)
    {
        spdlog::warn("No expected peers set, skipping wait");
        return true;
    }

    spdlog::info("Waiting for {} peers to connect (timeout: {}s)...", m_expected_peers, timeout_seconds);

    int elapsed = 0;
    while (m_running && elapsed < timeout_seconds)
    {
        size_t discovered = m_peer_count.load();
        size_t connected = 0;

        {
            std::lock_guard<std::mutex> lock(m_peers_mutex);
            for (const auto &peer : m_peers)
            {
                if (peer->connected.load())
                    connected++;
            }
        }

        spdlog::info("Peer status: {}/{} discovered, {}/{} connected", discovered, m_expected_peers, connected,
                     m_expected_peers);

        if (static_cast<int>(connected) >= m_expected_peers)
        {
            spdlog::info("All {} peers connected!", m_expected_peers);
            return true;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
    }

    spdlog::error("Timeout waiting for peers after {}s (discovered: {}, expected: {})", timeout_seconds,
                  m_peer_count.load(), m_expected_peers);
    return false;
}

std::string tcp_peer::get_local_ip() const
{
    try
    {
        boost::asio::io_context io;
        tcp::resolver resolver(io);
        tcp::resolver::query query(boost::asio::ip::host_name(), "");
        auto endpoints = resolver.resolve(query);

        for (const auto &ep : endpoints)
        {
            auto addr = ep.endpoint().address();
            if (addr.is_v4() && !addr.is_loopback())
            {
                return addr.to_string();
            }
        }
    }
    catch (...)
    {
    }
    return "127.0.0.1";
}

tcp_peer::stats tcp_peer::get_stats() const
{
    return stats{m_rays_enqueued.load(), m_rays_sent.load(),     m_rays_received.load(),        m_batches_sent.load(),
                 m_send_failures.load(), m_reconnections.load(), m_outbound_queue.size_approx()};
}

void tcp_peer::discover_peers(const std::string &namespace_name, const std::string &service_name)
{
    spdlog::info("Discovering peers in namespace {} service {} using ListInstances API", namespace_name, service_name);

    m_discovery_thread = std::thread([this, namespace_name, service_name]() {
        Aws::Client::ClientConfiguration config;
        std::string region = m_aws_region;
        if (region.empty())
        {
            const char *env_region = std::getenv("AWS_REGION");
            if (!env_region)
                env_region = std::getenv("AWS_DEFAULT_REGION");
            if (env_region)
                region = env_region;
        }
        if (!region.empty())
        {
            config.region = region;
            spdlog::info("Using region: {}", region);
        }

        Aws::ServiceDiscovery::ServiceDiscoveryClient client(config);

        Aws::ServiceDiscovery::Model::ListInstancesRequest request;
        request.SetServiceId(m_service_id);

        int retries = 0;
        while (m_running && retries < 60)
        {
            auto outcome = client.ListInstances(request);
            if (outcome.IsSuccess())
            {
                const auto &instances = outcome.GetResult().GetInstances();
                spdlog::info("Discovered {} instances via ListInstances", instances.size());

                for (const auto &instance : instances)
                {
                    auto attrs = instance.GetAttributes();
                    auto ip_it = attrs.find("AWS_INSTANCE_IPV4");
                    auto port_it = attrs.find("AWS_INSTANCE_PORT");
                    auto id_it = attrs.find("WORKER_ID");

                    if (ip_it != attrs.end())
                    {
                        peer_info peer;
                        peer.host = ip_it->second;
                        peer.port = (port_it != attrs.end()) ? std::stoi(port_it->second) : DEFAULT_TCP_PORT;
                        peer.worker_id = (id_it != attrs.end()) ? id_it->second : instance.GetId();

                        add_peer(peer);
                    }
                }

                if (m_expected_peers > 0 && static_cast<int>(m_peer_count.load()) >= m_expected_peers)
                {
                    spdlog::info("Discovered all {} expected peers", m_expected_peers);
                    return;
                }
            }
            else
            {
                spdlog::warn("ListInstances failed: {}", outcome.GetError().GetMessage());
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
            retries++;
        }

        spdlog::warn("Could not discover all peers after {} retries", retries);
    });
}

void tcp_peer::register_instance(const std::string &namespace_name, const std::string &service_name)
{
    m_namespace_name = namespace_name;
    m_service_name = service_name;
    m_instance_id = m_worker_id;

    if (m_service_id.empty())
    {
        spdlog::error("Cannot register instance: service_id is required");
        return;
    }

    spdlog::info("Registering instance {} with Cloud Map service {} (ID: {})", m_worker_id, service_name, m_service_id);

    try
    {
        Aws::ServiceDiscovery::ServiceDiscoveryClient client;

        Aws::ServiceDiscovery::Model::RegisterInstanceRequest request;
        request.SetServiceId(m_service_id);
        request.SetInstanceId(m_instance_id);

        Aws::Map<Aws::String, Aws::String> attributes;
        attributes["AWS_INSTANCE_IPV4"] = get_local_ip();
        attributes["AWS_INSTANCE_PORT"] = std::to_string(m_port);
        attributes["WORKER_ID"] = m_worker_id;
        request.SetAttributes(attributes);

        auto outcome = client.RegisterInstance(request);
        if (outcome.IsSuccess())
        {
            spdlog::info("Successfully registered instance {} with IP {} port {}", m_worker_id, get_local_ip(), m_port);
        }
        else
        {
            spdlog::error("Failed to register instance: {}", outcome.GetError().GetMessage());
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception during Cloud Map registration: {}", e.what());
    }
}

void tcp_peer::deregister_instance()
{
    if (m_instance_id.empty() || m_service_id.empty())
    {
        return;
    }

    spdlog::info("Deregistering instance {} from Cloud Map", m_instance_id);

    try
    {
        Aws::ServiceDiscovery::ServiceDiscoveryClient client;

        Aws::ServiceDiscovery::Model::DeregisterInstanceRequest request;
        request.SetServiceId(m_service_id);
        request.SetInstanceId(m_instance_id);

        auto outcome = client.DeregisterInstance(request);
        if (outcome.IsSuccess())
        {
            spdlog::info("Successfully deregistered instance {}", m_instance_id);
        }
        else
        {
            spdlog::warn("Failed to deregister instance: {}", outcome.GetError().GetMessage());
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Exception during Cloud Map deregistration: {}", e.what());
    }
}

// JSON serialization - uses the to_json/from_json defined in cloud_ray.hpp

std::vector<uint8_t> tcp_peer::serialize_rays_binary(const std::vector<models::cloud_ray> &rays)
{
    json j;
    j["rays"] = rays;
    std::string str = j.dump();
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::vector<models::cloud_ray> tcp_peer::deserialize_rays_binary(const std::vector<uint8_t> &data)
{
    std::string str(data.begin(), data.end());
    auto j = json::parse(str);
    return j["rays"].get<std::vector<models::cloud_ray>>();
}

std::vector<uint8_t> tcp_peer::serialize_pixels(const std::vector<models::pixel> &pixels)
{
    // Wire format: a top-level JSON array of pixels, mirroring what the
    // previous SQS sender produced. The Go backend parses each PIXEL_BATCH
    // message body as a single JSON array of pixel objects.
    json j = pixels;
    std::string str = j.dump();
    return std::vector<uint8_t>(str.begin(), str.end());
}

} // namespace cloud
