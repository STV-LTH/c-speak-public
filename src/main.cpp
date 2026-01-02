#include "AudioSystem.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <string>
#include <iomanip>

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

void print_usage() {
    std::cout << "\n⚡ ULTRA LOW LATENCY VOICE CHAT ⚡\n" << std::endl;
    std::cout << "Target: < 20ms round-trip latency" << std::endl;
    std::cout << "\nUsage:" << std::endl;
    std::cout << "  Local test:    ./voice" << std::endl;
    std::cout << "  Server:        ./voice server" << std::endl;
    std::cout << "  Client:        ./voice client <server_ip>" << std::endl;
    std::cout << "\nOptimizations:" << std::endl;
    std::cout << "  • 5ms audio frames" << std::endl;
    std::cout << "  • Opus complexity 0 (fastest)" << std::endl;
    std::cout << "  • No malloc in audio path" << std::endl;
    std::cout << "  • Busy-wait loops (no sleep)" << std::endl;
    std::cout << "  • Network priority maximized" << std::endl;
    std::cout << "\nExpected latency breakdown:" << std::endl;
    std::cout << "  • Audio capture:      ~5ms" << std::endl;
    std::cout << "  • Opus encode:        <1ms" << std::endl;
    std::cout << "  • Network send/recv:  ~1-2ms" << std::endl;
    std::cout << "  • Opus decode:        <1ms" << std::endl;
    std::cout << "  • Audio playback:     ~5ms" << std::endl;
    std::cout << "  • TOTAL:              ~12-15ms one way" << std::endl;
    std::cout << "  • ROUND-TRIP:         ~25-30ms" << std::endl;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    UltraLowLatencyAudio::Mode mode = UltraLowLatencyAudio::MODE_LOCAL_ECHO;
    std::string remote_ip = "";

    if (argc > 1) {
        std::string mode_str(argv[1]);

        if (mode_str == "server") {
            mode = UltraLowLatencyAudio::MODE_SERVER;
            std::cout << "🚀 ULTRA LOW LATENCY SERVER" << std::endl;
        }
        else if (mode_str == "client") {
            if (argc > 2) {
                mode = UltraLowLatencyAudio::MODE_CLIENT;
                remote_ip = argv[2];
                std::cout << "🚀 ULTRA LOW LATENCY CLIENT" << std::endl;
            } else {
                std::cerr << "❌ Need server IP" << std::endl;
                print_usage();
                return 1;
            }
        }
        else {
            print_usage();
            return 1;
        }
    } else {
        std::cout << "🚀 LOCAL LOW LATENCY TEST" << std::endl;
    }

    UltraLowLatencyAudio audio;

    std::cout << "Initializing ultra low latency system... " << std::flush;
    if (!audio.init(mode, remote_ip)) {
        std::cerr << "❌ FAILED" << std::endl;
        return 1;
    }
    std::cout << "✅ OK\n" << std::endl;

    std::cout << "==============================================" << std::endl;

    if (mode == UltraLowLatencyAudio::MODE_SERVER) {
        std::cout << "           LOW LATENCY SERVER              " << std::endl;
        std::cout << "         (Packet Relay Only)              " << std::endl;
    } else if (mode == UltraLowLatencyAudio::MODE_CLIENT) {
        std::cout << "           LOW LATENCY CLIENT              " << std::endl;
        std::cout << "   (Latency: <20ms target, ~30ms actual)   " << std::endl;
    } else {
        std::cout << "           LOCAL LATENCY TEST              " << std::endl;
        std::cout << "         (Echo: ~10ms target)              " << std::endl;
    }

    std::cout << "==============================================\n" << std::endl;

    if (mode == UltraLowLatencyAudio::MODE_SERVER) {
        std::cout << "📡 Listening: 0.0.0.0:" << NETWORK_PORT << std::endl;
        std::cout << "🔄 Relay mode: Instant packet forwarding" << std::endl;
        std::cout << "🔇 No audio I/O on server" << std::endl;
    } else if (mode == UltraLowLatencyAudio::MODE_CLIENT) {
        std::cout << "🎤 Audio input:  " << FRAME_SIZE << " samples ("
                  << std::fixed << std::setprecision(1) << (FRAME_SIZE * 1000.0 / SAMPLE_RATE)
                  << "ms frames)" << std::endl;
        std::cout << "🔊 Audio output: Same as input" << std::endl;
        std::cout << "📡 Connected to: " << remote_ip << ":" << NETWORK_PORT << std::endl;
        std::cout << "🔧 Opus: " << (OPUS_BITRATE/1000) << "kbps, complexity 0" << std::endl;
    }

    std::cout << "\n⏱️  Press Ctrl+C to exit" << std::endl;
    std::cout << "📊 Latency stats will appear in 2 seconds...\n" << std::endl;

    audio.start();

    auto start_time = std::chrono::steady_clock::now();

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (mode != UltraLowLatencyAudio::MODE_CLIENT) {
            std::cout << "\r⏱️  Uptime: " << elapsed << "s" << std::flush;
        }
    }

    std::cout << "\n\n🛑 Stopping ultra low latency system..." << std::endl;
    audio.stop();

    if (mode == UltraLowLatencyAudio::MODE_CLIENT) {
        uint64_t avg_latency = audio.get_average_latency_us();
        std::cout << "\n==============================================" << std::endl;
        std::cout << "              FINAL STATISTICS               " << std::endl;
        std::cout << "==============================================" << std::endl;
        std::cout << "Average one-way network latency: "
                  << std::fixed << std::setprecision(1) << (avg_latency / 1000.0) << "ms" << std::endl;
        std::cout << "Estimated round-trip latency:    "
                  << (avg_latency * 2 / 1000.0) << "ms" << std::endl;
        std::cout << "Audio pipeline: ~" << (FRAME_SIZE * 1000.0 / SAMPLE_RATE * 2)
                  << "ms (capture + playback)" << std::endl;
        std::cout << "Total perceived latency: ~"
                  << (avg_latency * 2 / 1000.0 + FRAME_SIZE * 2000.0 / SAMPLE_RATE)
                  << "ms" << std::endl;
    }

    std::cout << "\n👋 System stopped" << std::endl;

    return 0;
}
