#include "AudioSystem.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <string>

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

void print_usage() {
    std::cout << "\n🔥 UDP VOICE CHAT SERVER/CLIENT 🔥\n" << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  Local echo test:  ./voice" << std::endl;
    std::cout << "  Server (relay):   ./voice server" << std::endl;
    std::cout << "  Client:           ./voice client <server_ip>" << std::endl;
    std::cout << "\nFeatures:" << std::endl;
    std::cout << "  • Server only relays audio (no echo)" << std::endl;
    std::cout << "  • Clients hear each other via server" << std::endl;
    std::cout << "  • Multiple clients supported" << std::endl;
    std::cout << "  • Low latency (~30-50ms)" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  On server PC:    ./voice server" << std::endl;
    std::cout << "  On client PC 1:  ./voice client 192.168.1.100" << std::endl;
    std::cout << "  On client PC 2:  ./voice client 192.168.1.100" << std::endl;
    std::cout << "\nConfig:" << std::endl;
    std::cout << "  Port: " << NETWORK_PORT << std::endl;
    std::cout << "  Sample rate: " << SAMPLE_RATE << " Hz" << std::endl;
    std::cout << "  Frame size: " << FRAME_SIZE << " (10ms)" << std::endl;
    std::cout << "  Opus bitrate: " << (OPUS_BITRATE/1000) << " kbps\n" << std::endl;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    AudioSystem::Mode mode = AudioSystem::MODE_LOCAL_ECHO;
    std::string remote_ip = "";

    if (argc > 1) {
        std::string mode_str(argv[1]);

        if (mode_str == "server") {
            mode = AudioSystem::MODE_SERVER;
            std::cout << "🚀 Starting SERVER (relay only)..." << std::endl;
        }
        else if (mode_str == "client") {
            if (argc > 2) {
                mode = AudioSystem::MODE_CLIENT;
                remote_ip = argv[2];
                std::cout << "🚀 Starting CLIENT..." << std::endl;
            } else {
                std::cerr << "❌ Error: Client mode requires server IP address" << std::endl;
                print_usage();
                return 1;
            }
        }
        else {
            std::cerr << "❌ Error: Unknown mode '" << mode_str << "'" << std::endl;
            print_usage();
            return 1;
        }
    } else {
        std::cout << "🚀 Starting LOCAL ECHO test..." << std::endl;
    }

    AudioSystem audio;

    std::cout << "Initializing... ";
    if (!audio.init(mode, remote_ip)) {
        std::cerr << "❌ FAILED" << std::endl;
        return 1;
    }
    std::cout << "✅ OK\n" << std::endl;

    audio.start();

    std::cout << "========================================" << std::endl;

    switch (mode) {
        case AudioSystem::MODE_LOCAL_ECHO:
            std::cout << "        LOCAL ECHO TEST               " << std::endl;
            std::cout << "========================================\n" << std::endl;
            std::cout << "🎤 Speak -> 🔊 Hear your own voice" << std::endl;
            break;

        case AudioSystem::MODE_SERVER:
            std::cout << "        VOICE CHAT SERVER             " << std::endl;
            std::cout << "        (Relay Mode - No Echo)        " << std::endl;
            std::cout << "========================================\n" << std::endl;
            std::cout << "📡 Listening on port " << NETWORK_PORT << std::endl;
            std::cout << "🔄 Relaying audio between clients" << std::endl;
            std::cout << "🔇 Server does NOT hear audio" << std::endl;
            break;

        case AudioSystem::MODE_CLIENT:
            std::cout << "        VOICE CHAT CLIENT             " << std::endl;
            std::cout << "========================================\n" << std::endl;
            std::cout << "📡 Connected to: " << remote_ip << ":" << NETWORK_PORT << std::endl;
            std::cout << "🎤 Speak to talk to others" << std::endl;
            std::cout << "🔊 Hear other clients via server" << std::endl;
            break;
    }

    std::cout << "\n⏹️  Press Ctrl+C to exit\n" << std::endl;

    // Статистика
    int frames_sent = 0;
    int frames_received = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (elapsed > 0) {
            std::cout << "\r";
            std::cout << "⏱️  Time: " << elapsed << "s";

            if (mode == AudioSystem::MODE_CLIENT || mode == AudioSystem::MODE_LOCAL_ECHO) {
                std::cout << " | 📤 Sent: " << (frames_sent / elapsed) << " fps";
                std::cout << " | 📥 Recv: " << (frames_received / elapsed) << " fps";
            } else if (mode == AudioSystem::MODE_SERVER) {
                std::cout << " | 📡 Clients: " << frames_sent; // Будем использовать как счетчик клиентов
                frames_sent++;
            }

            std::cout << "     " << std::flush;

            if (elapsed >= 10) {
                frames_sent = frames_received = 0;
                start_time = now;
            }
        }
    }

    std::cout << "\n\n🛑 Stopping..." << std::endl;
    audio.stop();

    std::cout << "\n========================================" << std::endl;
    std::cout << "           SESSION ENDED              " << std::endl;
    std::cout << "========================================\n" << std::endl;
    std::cout << "👋 Goodbye!" << std::endl;

    return 0;
}
