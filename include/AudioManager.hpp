#pragma once

#include <portaudio.h>
#include <vector>
#include <functional>
#include <mutex>
#include <queue>
#include <memory>

class AudioManager {
public:
    static AudioManager& instance() {
        static AudioManager instance;
        return instance;
    }

    ~AudioManager();

    // Инициализация всей аудио системы
    bool init(int sampleRate = 48000, int framesPerBuffer = 960);

    // Запись
    void startCapture();
    void stopCapture();
    void setCaptureCallback(std::function<void(const std::vector<float>&)> callback);

    // Воспроизведение
    void play(const std::vector<float>& audioData);
    void startPlayback();
    void stopPlayback();

    // Статус
    bool isInitialized() const { return initialized_; }

private:
    AudioManager(); // Приватный конструктор
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // PortAudio callbacks
    static int captureCallback(const void* input, void* output,
                              unsigned long frameCount,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData);

    static int playbackCallback(const void* input, void* output,
                               unsigned long frameCount,
                               const PaStreamCallbackTimeInfo* timeInfo,
                               PaStreamCallbackFlags statusFlags,
                               void* userData);

private:
    bool initialized_ = false;
    int sampleRate_ = 48000;
    int framesPerBuffer_ = 960;

    // Capture
    PaStream* captureStream_ = nullptr;
    std::function<void(const std::vector<float>&)> captureCallback_;

    // Playback
    PaStream* playbackStream_ = nullptr;
    std::queue<std::vector<float>> playbackQueue_;
    std::mutex playbackMutex_;
};
