#include "../include/AudioManager.hpp"
#include <iostream>

AudioManager::AudioManager() {}

AudioManager::~AudioManager() {
    // Останавливаем потоки если они запущены
    if (playbackStream_) {
        if (Pa_IsStreamActive(playbackStream_)) {
            Pa_StopStream(playbackStream_);
        }
        Pa_CloseStream(playbackStream_);
        playbackStream_ = nullptr;
    }

    if (captureStream_) {
        if (Pa_IsStreamActive(captureStream_)) {
            Pa_StopStream(captureStream_);
        }
        Pa_CloseStream(captureStream_);
        captureStream_ = nullptr;
    }

    // Terminate только если был Initialize
    if (initialized_) {
        Pa_Terminate();
        initialized_ = false;
    }
}

bool AudioManager::init(int sampleRate, int framesPerBuffer) {
    if (initialized_) return true;

    sampleRate_ = sampleRate;
    framesPerBuffer_ = framesPerBuffer;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

void AudioManager::startCapture() {
    if (!initialized_ || captureStream_) return;

    PaError err = Pa_OpenDefaultStream(&captureStream_,
                                       1,          // input channels
                                       0,          // output channels
                                       paFloat32,  // sample format
                                       sampleRate_,
                                       framesPerBuffer_,
                                       captureCallback,
                                       this);

    if (err != paNoError) {
        std::cerr << "Capture stream open error: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    err = Pa_StartStream(captureStream_);
    if (err != paNoError) {
        std::cerr << "Capture start error: " << Pa_GetErrorText(err) << std::endl;
    }
}

void AudioManager::stopCapture() {
    if (captureStream_) {
        Pa_StopStream(captureStream_);
        Pa_CloseStream(captureStream_);
        captureStream_ = nullptr;
    }
}

void AudioManager::setCaptureCallback(std::function<void(const std::vector<float>&)> callback) {
    captureCallback_ = callback;
}

int AudioManager::captureCallback(const void* input, void* output,
                                 unsigned long frameCount,
                                 const PaStreamCallbackTimeInfo* timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void* userData) {
    (void)output;
    (void)timeInfo;
    (void)statusFlags;

    AudioManager* self = static_cast<AudioManager*>(userData);
    if (self && self->captureCallback_ && input) {
        const float* samples = static_cast<const float*>(input);
        std::vector<float> buffer(samples, samples + frameCount);
        self->captureCallback_(buffer);
    }
    return 0;
}

void AudioManager::play(const std::vector<float>& audioData) {
    if (!initialized_ || audioData.empty()) return;

    std::lock_guard<std::mutex> lock(playbackMutex_);
    playbackQueue_.push(audioData);
}

void AudioManager::startPlayback() {
    if (!initialized_ || playbackStream_) return;

    PaError err = Pa_OpenDefaultStream(&playbackStream_,
                                       0,          // input channels
                                       1,          // output channels
                                       paFloat32,  // sample format
                                       sampleRate_,
                                       framesPerBuffer_,
                                       playbackCallback,
                                       this);

    if (err != paNoError) {
        std::cerr << "Playback stream open error: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    err = Pa_StartStream(playbackStream_);
    if (err != paNoError) {
        std::cerr << "Playback start error: " << Pa_GetErrorText(err) << std::endl;
    }
}

void AudioManager::stopPlayback() {
    if (playbackStream_) {
        Pa_StopStream(playbackStream_);
        Pa_CloseStream(playbackStream_);
        playbackStream_ = nullptr;
    }

    std::lock_guard<std::mutex> lock(playbackMutex_);
    std::queue<std::vector<float>> empty;
    std::swap(playbackQueue_, empty);
}

int AudioManager::playbackCallback(const void* input, void* output,
                                  unsigned long frameCount,
                                  const PaStreamCallbackTimeInfo* timeInfo,
                                  PaStreamCallbackFlags statusFlags,
                                  void* userData) {
    (void)input;
    (void)timeInfo;
    (void)statusFlags;

    AudioManager* self = static_cast<AudioManager*>(userData);
    if (!self || !output) return 0;

    float* out = static_cast<float*>(output);
    std::lock_guard<std::mutex> lock(self->playbackMutex_);

    if (!self->playbackQueue_.empty()) {
        auto& data = self->playbackQueue_.front();
        size_t samplesToCopy = std::min(static_cast<size_t>(frameCount), data.size());

        // Копируем данные
        for (size_t i = 0; i < samplesToCopy; ++i) {
            out[i] = data[i];
        }

        // Заполняем остаток нулями
        for (size_t i = samplesToCopy; i < frameCount; ++i) {
            out[i] = 0.0f;
        }

        // Удаляем обработанные данные
        if (samplesToCopy == data.size()) {
            self->playbackQueue_.pop();
        } else {
            // Оставляем остаток
            self->playbackQueue_.front() = std::vector<float>(
                data.begin() + samplesToCopy, data.end()
            );
        }
    } else {
        // Нет данных - тишина
        for (size_t i = 0; i < frameCount; ++i) {
            out[i] = 0.0f;
        }
    }

    return 0;
}
