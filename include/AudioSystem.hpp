#pragma once

#include <portaudio.h>
#include <opus/opus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
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

// ==================== CONFIG ====================
constexpr int SAMPLE_RATE = 48000;
constexpr int FRAME_SIZE = 480;      // 10ms
constexpr int CHANNELS = 1;
constexpr int OPUS_BITRATE = 32000;
constexpr int NETWORK_PORT = 12345;

// ==================== NETWORK CLASS ====================
class Network {
public:
    Network() : sockfd(-1), running(false) {}

    ~Network() { stop(); }

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

    bool send_to(const std::vector<unsigned char>& data, const sockaddr_in& addr) {
        if (sockfd == -1) return false;

        socklen_t addr_len = sizeof(addr);
        int sent = sendto(sockfd, data.data(), data.size(), 0,
                         (struct sockaddr*)&addr, addr_len);

        return sent == static_cast<int>(data.size());
    }

    bool send(const std::vector<unsigned char>& data) {
        return send_to(data, peer_addr);
    }

    bool receive(std::vector<unsigned char>& data, sockaddr_in& from_addr) {
        if (sockfd == -1) return false;

        char buffer[4096];
        socklen_t addr_len = sizeof(from_addr);

        int received = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT,
                               (struct sockaddr*)&from_addr, &addr_len);

        if (received > 0) {
            data.assign(buffer, buffer + received);
            return true;
        }

        return false;
    }

private:
    bool create_socket(const std::string& bind_ip, int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) return false;

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        // Allow multiple clients to bind to same port
        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

// ==================== AUDIO SYSTEM ====================
class AudioSystem {
public:
    enum Mode {
        MODE_LOCAL_ECHO,     // Локальный эхо-тест
        MODE_SERVER,         // Сервер (ретранслятор)
        MODE_CLIENT          // Клиент
    };

    AudioSystem() :
        pa_initialized(false),
        running(false),
        mode(MODE_LOCAL_ECHO),
        sequence_number(0) {}

    ~AudioSystem() { stop(); }

    bool init(Mode m, const std::string& remote_ip = "") {
        mode = m;

        // PortAudio инициализируем только если нужно захватывать/воспроизводить звук
        if (mode != MODE_SERVER) {
            if (Pa_Initialize() != paNoError) {
                std::cerr << "❌ PortAudio init failed" << std::endl;
                return false;
            }
            pa_initialized = true;

            // Только клиенты захватывают звук с микрофона
            if (mode == MODE_CLIENT) {
                if (Pa_OpenDefaultStream(&capture_stream, 1, 0, paFloat32,
                                        SAMPLE_RATE, FRAME_SIZE, capture_cb, this) != paNoError) {
                    std::cerr << "❌ Capture stream failed" << std::endl;
                    return false;
                }
            }

            // Все кроме сервера воспроизводят звук
            if (mode != MODE_SERVER) {
                if (Pa_OpenDefaultStream(&playback_stream, 0, 1, paFloat32,
                                        SAMPLE_RATE, FRAME_SIZE, playback_cb, this) != paNoError) {
                    std::cerr << "❌ Playback stream failed" << std::endl;
                    if (capture_stream) Pa_CloseStream(capture_stream);
                    return false;
                }
            }
        }

        // Opus для всех режимов
        int err;
        encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
        decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);

        if (!encoder || !decoder) {
            std::cerr << "❌ Opus init failed" << std::endl;
            return false;
        }

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
        opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));

        // Network
        if (mode != MODE_LOCAL_ECHO) {
            if (!init_network(mode == MODE_SERVER ? "" : remote_ip)) {
                std::cerr << "❌ Network init failed" << std::endl;
                return false;
            }
        }

        return true;
    }

    void start() {
        if (!running) {
            running = true;

            if (capture_stream) Pa_StartStream(capture_stream);
            if (playback_stream) Pa_StartStream(playback_stream);

            if (mode != MODE_LOCAL_ECHO) {
                network_thread = std::thread(&AudioSystem::network_loop, this);
            }
        }
    }

    void stop() {
        if (running) {
            running = false;

            if (mode != MODE_LOCAL_ECHO && network_thread.joinable()) {
                network_thread.join();
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

            if (encoder) {
                opus_encoder_destroy(encoder);
                encoder = nullptr;
            }

            if (decoder) {
                opus_decoder_destroy(decoder);
                decoder = nullptr;
            }

            if (pa_initialized) {
                Pa_Terminate();
                pa_initialized = false;
            }

            // Clean queues
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                while (!audio_queue.empty()) audio_queue.pop();
            }
            {
                std::lock_guard<std::mutex> lock(net_queue_mutex);
                while (!network_queue.empty()) network_queue.pop();
            }

            // Clean clients
            clients.clear();
        }
    }

private:
    bool init_network(const std::string& remote_ip) {
        if (remote_ip.empty()) {
            // Server mode
            std::cout << "🔌 Server mode (port " << NETWORK_PORT << ")" << std::endl;
            return network.start_server(NETWORK_PORT);
        } else {
            // Client mode
            std::cout << "🔌 Client mode (connecting to " << remote_ip << ":" << NETWORK_PORT << ")" << std::endl;
            return network.start_client(remote_ip, NETWORK_PORT);
        }
    }

    void network_loop() {
        std::vector<unsigned char> buffer;
        sockaddr_in from_addr;

        while (running) {
            // Принимаем данные от всех клиентов
            if (network.receive(buffer, from_addr)) {
                if (buffer.size() > sizeof(uint32_t)) {
                    // Извлекаем sequence number
                    uint32_t seq_num;
                    memcpy(&seq_num, buffer.data(), sizeof(seq_num));

                    std::vector<unsigned char> audio_data(buffer.begin() + sizeof(seq_num), buffer.end());

                    if (mode == MODE_SERVER) {
                        // Сервер: ретранслируем всем клиентам кроме отправителя
                        broadcast_audio(audio_data, from_addr);
                    } else {
                        // Клиент: декодируем и воспроизводим
                        float decoded[FRAME_SIZE];
                        int samples = opus_decode_float(decoder, audio_data.data(), audio_data.size(),
                                                       decoded, FRAME_SIZE, 0);

                        if (samples > 0) {
                            std::vector<float> audio(decoded, decoded + samples);

                            std::lock_guard<std::mutex> lock(queue_mutex);
                            audio_queue.push(std::move(audio));
                        }
                    }

                    // Запоминаем клиента (для сервера)
                    if (mode == MODE_SERVER) {
                        std::string client_key = get_client_key(from_addr);
                        if (clients.find(client_key) == clients.end()) {
                            clients[client_key] = from_addr;
                            std::cout << "📱 New client connected: " << client_key << std::endl;
                        }
                    }
                }
            }

            // Отправляем данные (только клиенты отправляют)
            if (mode == MODE_CLIENT) {
                std::lock_guard<std::mutex> lock(net_queue_mutex);
                if (!network_queue.empty()) {
                    auto data = network_queue.front();
                    network_queue.pop();

                    std::vector<unsigned char> packet;
                    packet.resize(sizeof(sequence_number) + data.size());

                    memcpy(packet.data(), &sequence_number, sizeof(sequence_number));
                    memcpy(packet.data() + sizeof(sequence_number), data.data(), data.size());

                    sequence_number++;

                    network.send(packet);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void broadcast_audio(const std::vector<unsigned char>& audio_data, const sockaddr_in& exclude_addr) {
        std::vector<unsigned char> packet;
        packet.resize(sizeof(sequence_number) + audio_data.size());

        memcpy(packet.data(), &sequence_number, sizeof(sequence_number));
        memcpy(packet.data() + sizeof(sequence_number), audio_data.data(), audio_data.size());

        sequence_number++;

        // Отправляем всем клиентам кроме отправителя
        for (const auto& [key, client_addr] : clients) {
            // Не отправляем обратно отправителю
            if (client_addr.sin_addr.s_addr == exclude_addr.sin_addr.s_addr &&
                client_addr.sin_port == exclude_addr.sin_port) {
                continue;
            }

            network.send_to(packet, client_addr);
        }
    }

    std::string get_client_key(const sockaddr_in& addr) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    static int capture_cb(const void* input, void* output, unsigned long frame_count,
                         const PaStreamCallbackTimeInfo* time_info, PaStreamCallbackFlags flags, void* user_data) {
        (void)output; (void)time_info; (void)flags;

        AudioSystem* self = static_cast<AudioSystem*>(user_data);
        if (input && self && self->running && self->mode == MODE_CLIENT) {
            self->capture_audio(static_cast<const float*>(input), frame_count);
        }
        return 0;
    }

    static int playback_cb(const void* input, void* output, unsigned long frame_count,
                          const PaStreamCallbackTimeInfo* time_info, PaStreamCallbackFlags flags, void* user_data) {
        (void)input; (void)time_info; (void)flags;

        AudioSystem* self = static_cast<AudioSystem*>(user_data);
        if (!output || !self || !self->running || self->mode == MODE_SERVER) return 0;

        float* out = static_cast<float*>(output);
        std::lock_guard<std::mutex> lock(self->queue_mutex);

        if (!self->audio_queue.empty()) {
            auto& data = self->audio_queue.front();
            size_t to_copy = std::min(data.size(), static_cast<size_t>(frame_count));

            memcpy(out, data.data(), to_copy * sizeof(float));

            if (to_copy == data.size()) {
                self->audio_queue.pop();
            } else {
                self->audio_queue.front() = std::vector<float>(data.begin() + to_copy, data.end());
            }

            if (to_copy < frame_count) {
                memset(out + to_copy, 0, (frame_count - to_copy) * sizeof(float));
            }
        } else {
            memset(out, 0, frame_count * sizeof(float));
        }

        return 0;
    }

    void capture_audio(const float* input, unsigned long frame_count) {
        // Кодируем аудио
        unsigned char encoded[400];
        int bytes = opus_encode_float(encoder, input, frame_count, encoded, sizeof(encoded));
        if (bytes <= 0) return;

        // Отправляем в сетевую очередь
        std::vector<unsigned char> data(encoded, encoded + bytes);
        std::lock_guard<std::mutex> lock(net_queue_mutex);
        network_queue.push(std::move(data));
    }

private:
    bool pa_initialized;
    std::atomic<bool> running;
    Mode mode;

    PaStream* capture_stream = nullptr;   // Только у клиента
    PaStream* playback_stream = nullptr;  // У клиента и локального эхо

    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;

    // Очередь для воспроизведения
    std::queue<std::vector<float>> audio_queue;
    std::mutex queue_mutex;

    // Сеть
    Network network;
    std::queue<std::vector<unsigned char>> network_queue;
    std::mutex net_queue_mutex;
    std::thread network_thread;
    uint32_t sequence_number;

    // Список клиентов (только для сервера)
    std::map<std::string, sockaddr_in> clients;
};
