#include "../include/AudioPlayer.hpp"
#include <iostream>
#include <algorithm>

AudioPlayer::AudioPlayer() : stream(nullptr) {}

AudioPlayer::~AudioPlayer() {
    stop();
}

bool AudioPlayer::init(int sampleRate, int framesPerBuffer) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_OpenDefaultStream(&stream,
                               0,          // input channels
                               1,          // output channels
                               paFloat32,  // sample format
                               sampleRate,
                               framesPerBuffer,
                               paCallback,
                               this);

    if (err != paNoError) {
        std::cerr << "PortAudio stream error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return false;
    }

    return true;
}

void AudioPlayer::play(const std::vector<float>& audioData) {
    if (audioData.empty()) return;

    std::lock_guard<std::mutex> lock(queueMutex);
    audioQueue.push(audioData);
}

void AudioPlayer::start() {
    if (stream) {
        PaError err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
        }
    }
}

void AudioPlayer::stop() {
    if (stream) {
        if (Pa_IsStreamActive(stream)) {
            Pa_StopStream(stream);
        }
        Pa_CloseStream(stream);
        stream = nullptr;
    }
    Pa_Terminate();

    std::lock_guard<std::mutex> lock(queueMutex);
    std::queue<std::vector<float>> empty;
    std::swap(audioQueue, empty);
}

int AudioPlayer::paCallback(const void* input, void* output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData) {
    (void)input;
    (void)timeInfo;
    (void)statusFlags;

    AudioPlayer* self = static_cast<AudioPlayer*>(userData);
    if (!self || !output) return 0;

    std::lock_guard<std::mutex> lock(self->queueMutex);
    float* out = static_cast<float*>(output);

    if (!self->audioQueue.empty()) {
        auto& data = self->audioQueue.front();
        size_t samplesToCopy = std::min(static_cast<size_t>(frameCount), data.size());

        // Копируем данные
        for (size_t i = 0; i < samplesToCopy; ++i) {
            out[i] = data[i];
        }

        // Заполняем остаток нулями
        for (size_t i = samplesToCopy; i < frameCount; ++i) {
            out[i] = 0.0f;
        }

        // Обновляем или удаляем буфер
        if (samplesToCopy == data.size()) {
            self->audioQueue.pop();
        } else {
            // Оставляем остаток
            self->audioQueue.front() = std::vector<float>(
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
