#pragma once

#include <portaudio.h>
#include <vector>
#include <mutex>
#include <queue>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool init(int sampleRate = 48000, int framesPerBuffer = 960);
    void play(const std::vector<float>& audioData);
    void start();
    void stop();

private:
    PaStream* stream;
    std::queue<std::vector<float>> audioQueue;
    std::mutex queueMutex;

    static int paCallback(const void* input, void* output,
                         unsigned long frameCount,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData);
};
