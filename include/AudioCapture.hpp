#pragma once

#include <portaudio.h>
#include <vector>
#include <functional>

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    bool init(int sampleRate = 48000, int framesPerBuffer = 960);
    void start();
    void stop();
    void setCallback(std::function<void(const std::vector<float>&)> callback);

private:
    PaStream* stream;
    std::function<void(const std::vector<float>&)> dataCallback;

    static int paCallback(const void* input, void* output,
                         unsigned long frameCount,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData);
};
