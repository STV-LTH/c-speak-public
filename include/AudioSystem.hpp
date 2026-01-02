#pragma once

#include <portaudio.h>
#include <opus/opus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>  // Добавил для TCP_NODELAY
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <iostream>
#include <cstring>
#include <chrono>
#include <string>
#include <map>
#include <algorithm>
#include <deque>

// ==================== ULTRA LOW LATENCY CONFIG ====================
constexpr int SAMPLE_RATE = 48000;
constexpr int FRAME_SIZE = 240;      // 5ms вместо 10ms!
constexpr int CHANNELS = 1;
constexpr int OPUS_BITRATE = 32000;  // Минимальный но качественный
constexpr int NETWORK_PORT = 12345;
constexpr int MAX_NETWORK_QUEUE = 3; // Очередь всего на 3 пакета (15ms)

// ==================== HIGH PRIORITY NETWORK ====================
class LowLatencyNetwork {
public:
    LowLatencyNetwork() : sockfd(-1), running(false) {}

    ~LowLatencyNetwork() { stop(); }

    bool start_server(int port) {
        return create_socket("0.0.0.0", port);
    }

    bool start_client(const std::string& server_ip, int port) {
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(port);
        inet_pton(AF_INET, server_ip.c_str(), &peer_addr.sin_addr);

        return create_socket("0.0.0.0", 0);
    }

    void stop() {
        running = false;
        if (sockfd != -1) {
            close(sockfd);
            sockfd = -1;
        }
    }

    // NON-BLOCKING отправка
    bool send_urgent(const std::vector<unsigned char>& data) {
        if (sockfd == -1) return false;

        socklen_t addr_len = sizeof(peer_addr);
        int sent = sendto(sockfd, data.data(), data.size(),
                         MSG_DONTWAIT | MSG_NOSIGNAL,
                         (struct sockaddr*)&peer_addr, addr_len);

        return sent == static_cast<int>(data.size());
    }

    // NON-BLOCKING получение
    bool receive_urgent(std::vector<unsigned char>& data, sockaddr_in& from_addr) {
        if (sockfd == -1) return false;

        char buffer[1024];
        socklen_t addr_len = sizeof(from_addr);

        int received = recvfrom(sockfd, buffer, sizeof(buffer),
                               MSG_DONTWAIT,
                               (struct sockaddr*)&from_addr, &addr_len);

        if (received > 0) {
            data.assign(buffer, buffer + received);
            return true;
        }

        return false;
    }

    // Геттер для sockfd (нужен для relay)
    int get_sockfd() const { return sockfd; }

private:
    bool create_socket(const std::string& bind_ip, int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) return false;

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        int buf_size = 65536;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

        sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(port);
        inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr);

        if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        running = true;
        return true;
    }

private:
    int sockfd;
    std::atomic<bool> running;
    sockaddr_in peer_addr;
};

// ==================== ZERO COPY AUDIO SYSTEM ====================
class UltraLowLatencyAudio {
public:
    enum Mode {
        MODE_LOCAL_ECHO,
        MODE_SERVER,
        MODE_CLIENT
    };

    UltraLowLatencyAudio() :
        pa_initialized(false),
        running(false),
        mode(MODE_LOCAL_ECHO),
        sequence_number(0),
        total_latency_us(0),
        packets_received(0) {}

    ~UltraLowLatencyAudio() { stop(); }

    bool init(Mode m, const std::string& remote_ip = "") {
        mode = m;

        if (mode != MODE_SERVER) {
            PaError err = Pa_Initialize();
            if (err != paNoError) {
                std::cerr << "❌ PortAudio failed: " << Pa_GetErrorText(err) << std::endl;
                return false;
            }
            pa_initialized = true;

            if (mode == MODE_CLIENT) {
                err = Pa_OpenDefaultStream(&capture_stream, 1, 0, paFloat32,
                                          SAMPLE_RATE, FRAME_SIZE, capture_cb, this);
                if (err != paNoError) {
                    std::cerr << "❌ Capture failed: " << Pa_GetErrorText(err) << std::endl;
                    return false;
                }
            }

            if (mode != MODE_SERVER) {
                err = Pa_OpenDefaultStream(&playback_stream, 0, 1, paFloat32,
                                          SAMPLE_RATE, FRAME_SIZE, playback_cb, this);
                if (err != paNoError) {
                    std::cerr << "❌ Playback failed: " << Pa_GetErrorText(err) << std::endl;
                    if (capture_stream) Pa_CloseStream(capture_stream);
                    return false;
                }
            }
        }

        int err_code;
        encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err_code);
        decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err_code);

        if (!encoder || !decoder) return false;

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
        opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));
        opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0));
        opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(0));

        opus_decoder_ctl(decoder, OPUS_SET_COMPLEXITY(0));

        if (mode != MODE_LOCAL_ECHO) {
            if (!init_network(mode == MODE_SERVER ? "" : remote_ip)) {
                return false;
            }
        }

        std::cout << "⚡ Ultra low latency mode enabled" << std::endl;
        std::cout << "📊 Frame size: " << FRAME_SIZE << " samples ("
                  << (FRAME_SIZE * 1000.0 / SAMPLE_RATE) << "ms)" << std::endl;

        return true;
    }

    void start() {
        if (!running) {
            running = true;
            start_time = std::chrono::steady_clock::now();

            if (capture_stream) Pa_StartStream(capture_stream);
            if (playback_stream) Pa_StartStream(playback_stream);

            if (mode != MODE_LOCAL_ECHO) {
                network_thread = std::thread(&UltraLowLatencyAudio::network_loop, this);
            }

            if (mode == MODE_CLIENT) {
                stats_thread = std::thread(&UltraLowLatencyAudio::stats_loop, this);
            }
        }
    }

    void stop() {
        if (running) {
            running = false;

            if (mode != MODE_LOCAL_ECHO && network_thread.joinable()) {
                network_thread.join();
            }

            if (stats_thread.joinable()) {
                stats_thread.join();
            }

            if (capture_stream) {
                Pa_StopStream(capture_stream);
                Pa_CloseStream(capture_stream);
                capture_stream = nullptr;
            }

            if (playback_stream) {
                Pa_StopStream(playback_stream);
                Pa_CloseStream(playback_stream);
                playback_stream = nullptr;
            }

            if (encoder) opus_encoder_destroy(encoder);
            if (decoder) opus_decoder_destroy(decoder);

            if (pa_initialized) Pa_Terminate();

            audio_buffer.clear();
            network_queue.clear();
            clients.clear();
        }
    }

    uint64_t get_average_latency_us() const {
        return packets_received > 0 ? total_latency_us / packets_received : 0;
    }

private:
    bool init_network(const std::string& remote_ip) {
        if (remote_ip.empty()) {
            std::cout << "🔌 Server (port " << NETWORK_PORT << ")" << std::endl;
            return network.start_server(NETWORK_PORT);
        } else {
            std::cout << "🔌 Client -> " << remote_ip << ":" << NETWORK_PORT << std::endl;
            return network.start_client(remote_ip, NETWORK_PORT);
        }
    }

    void network_loop() {
        std::vector<unsigned char> buffer;
        sockaddr_in from_addr;

        float decode_buffer[FRAME_SIZE];

        while (running) {
            auto loop_start = std::chrono::steady_clock::now();

            while (network.receive_urgent(buffer, from_addr)) {
                if (buffer.size() > sizeof(uint64_t) + sizeof(uint32_t)) {
                    uint64_t sent_timestamp;
                    uint32_t seq_num;

                    memcpy(&sent_timestamp, buffer.data(), sizeof(sent_timestamp));
                    memcpy(&seq_num, buffer.data() + sizeof(sent_timestamp), sizeof(seq_num));

                    std::vector<unsigned char> audio_data(
                        buffer.begin() + sizeof(sent_timestamp) + sizeof(seq_num),
                        buffer.end()
                    );

                    if (mode == MODE_SERVER) {
                        relay_audio(audio_data, sent_timestamp, seq_num, from_addr);
                    } else {
                        int samples = opus_decode_float(decoder,
                            audio_data.data(), audio_data.size(),
                            decode_buffer, FRAME_SIZE, 0);

                        if (samples > 0) {
                            auto now = std::chrono::steady_clock::now();
                            auto sent_time = std::chrono::steady_clock::time_point(
                                std::chrono::microseconds(sent_timestamp));
                            uint64_t latency = std::chrono::duration_cast<std::chrono::microseconds>(
                                now - sent_time).count();

                            total_latency_us += latency;
                            packets_received++;

                            std::lock_guard<std::mutex> lock(audio_mutex);
                            if (audio_buffer.size() < MAX_NETWORK_QUEUE) {
                                audio_buffer.push_back(std::vector<float>(
                                    decode_buffer, decode_buffer + samples));
                            }
                        }
                    }

                    if (mode == MODE_SERVER) {
                        std::string key = get_client_key(from_addr);
                        clients[key] = from_addr;
                    }
                }
            }

            if (mode == MODE_CLIENT) {
                std::vector<std::vector<unsigned char>> to_send;
                {
                    std::lock_guard<std::mutex> lock(network_mutex);
                    to_send.swap(network_queue);
                }

                for (auto& data : to_send) {
                    if (!data.empty()) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                            now.time_since_epoch()).count();

                        std::vector<unsigned char> packet;
                        packet.resize(sizeof(timestamp) + sizeof(sequence_number) + data.size());

                        memcpy(packet.data(), &timestamp, sizeof(timestamp));
                        memcpy(packet.data() + sizeof(timestamp), &sequence_number, sizeof(sequence_number));
                        memcpy(packet.data() + sizeof(timestamp) + sizeof(sequence_number),
                               data.data(), data.size());

                        sequence_number++;
                        network.send_urgent(packet);
                    }
                }
            }

            auto loop_end = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                loop_end - loop_start).count();

            const int64_t target_loop_time = 5000;
            if (elapsed < target_loop_time) {
                auto wait_until = loop_start + std::chrono::microseconds(target_loop_time);
                while (std::chrono::steady_clock::now() < wait_until) {
                    // busy wait
                }
            }
        }
    }

    void relay_audio(const std::vector<unsigned char>& audio_data,
                     uint64_t timestamp, uint32_t seq_num,
                     const sockaddr_in& exclude_addr) {
        std::vector<unsigned char> packet;
        packet.resize(sizeof(timestamp) + sizeof(seq_num) + audio_data.size());

        memcpy(packet.data(), &timestamp, sizeof(timestamp));
        memcpy(packet.data() + sizeof(timestamp), &seq_num, sizeof(seq_num));
        memcpy(packet.data() + sizeof(timestamp) + sizeof(seq_num),
               audio_data.data(), audio_data.size());

        for (const auto& [key, client_addr] : clients) {
            if (client_addr.sin_addr.s_addr == exclude_addr.sin_addr.s_addr &&
                client_addr.sin_port == exclude_addr.sin_port) {
                continue;
            }

            socklen_t addr_len = sizeof(client_addr);
            sendto(network.get_sockfd(), packet.data(), packet.size(),
                   MSG_DONTWAIT | MSG_NOSIGNAL,
                   (struct sockaddr*)&client_addr, addr_len);
        }
    }

    void stats_loop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (packets_received > 0) {
                uint64_t avg_latency = total_latency_us / packets_received;
                std::cout << "\r📊 Latency: " << (avg_latency / 1000.0) << "ms avg | "
                          << "Packets: " << packets_received
                          << " | Buffer: " << audio_buffer.size()
                          << "       " << std::flush;

                total_latency_us = 0;
                packets_received = 0;
            }
        }
    }

    std::string get_client_key(const sockaddr_in& addr) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    static int capture_cb(const void* input, void* output, unsigned long frame_count,
                         const PaStreamCallbackTimeInfo* time_info,
                         PaStreamCallbackFlags flags, void* user_data) {
        (void)output; (void)time_info; (void)flags;

        UltraLowLatencyAudio* self = static_cast<UltraLowLatencyAudio*>(user_data);
        if (input && self && self->running && self->mode == MODE_CLIENT) {
            self->encode_and_queue(static_cast<const float*>(input), frame_count);
        }
        return 0;
    }

    static int playback_cb(const void* input, void* output, unsigned long frame_count,
                          const PaStreamCallbackTimeInfo* time_info,
                          PaStreamCallbackFlags flags, void* user_data) {
        (void)input; (void)time_info; (void)flags;

        UltraLowLatencyAudio* self = static_cast<UltraLowLatencyAudio*>(user_data);
        if (!output || !self || !self->running || self->mode == MODE_SERVER) {
            return 0;
        }

        float* out = static_cast<float*>(output);
        std::lock_guard<std::mutex> lock(self->audio_mutex);

        if (!self->audio_buffer.empty()) {
            auto& data = self->audio_buffer.front();
            size_t to_copy = std::min(data.size(), static_cast<size_t>(frame_count));

            memcpy(out, data.data(), to_copy * sizeof(float));

            if (to_copy == data.size()) {
                self->audio_buffer.pop_front();
            } else {
                self->audio_buffer.front() = std::vector<float>(
                    data.begin() + to_copy, data.end());
            }

            if (to_copy < frame_count) {
                memset(out + to_copy, 0, (frame_count - to_copy) * sizeof(float));
            }
        } else {
            memset(out, 0, frame_count * sizeof(float));
        }

        return 0;
    }

    void encode_and_queue(const float* input, unsigned long frame_count) {
        static unsigned char encoded[512];

        int bytes = opus_encode_float(encoder, input, frame_count, encoded, sizeof(encoded));
        if (bytes <= 0) return;

        std::vector<unsigned char> data(encoded, encoded + bytes);

        std::lock_guard<std::mutex> lock(network_mutex);
        if (network_queue.size() < MAX_NETWORK_QUEUE) {
            network_queue.push_back(std::move(data));
        }
    }

private:
    bool pa_initialized;
    std::atomic<bool> running;
    Mode mode;

    PaStream* capture_stream = nullptr;
    PaStream* playback_stream = nullptr;

    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;

    std::deque<std::vector<float>> audio_buffer;
    std::mutex audio_mutex;

    LowLatencyNetwork network;
    std::vector<std::vector<unsigned char>> network_queue;
    std::mutex network_mutex;
    std::thread network_thread;
    std::thread stats_thread;

    uint32_t sequence_number;
    std::chrono::steady_clock::time_point start_time;

    uint64_t total_latency_us;
    uint64_t packets_received;

    std::map<std::string, sockaddr_in> clients;
};
