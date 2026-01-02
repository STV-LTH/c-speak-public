#include "../include/AudioCapture.hpp"
#include <iostream>

AudioCapture::AudioCapture() : stream(nullptr) {}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::init(int sampleRate, int framesPerBuffer) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_OpenDefaultStream(&stream,
                               1,          // input channels
                               0,          // output channels
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

void AudioCapture::start() {
    if (stream) {
        PaError err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
        }
    }
}

void AudioCapture::stop() {
    if (stream) {
        if (Pa_IsStreamActive(stream)) {
            Pa_StopStream(stream);
        }
        Pa_CloseStream(stream);
        stream = nullptr;
    }
    Pa_Terminate();
}

void AudioCapture::setCallback(std::function<void(const std::vector<float>&)> callback) {
    dataCallback = callback;
}

int AudioCapture::paCallback(const void* input, void* output,
                            unsigned long frameCount,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void* userData) {
    (void)output;
    (void)timeInfo;
    (void)statusFlags;

    AudioCapture* self = static_cast<AudioCapture*>(userData);
    if (self && self->dataCallback && input) {
        const float* samples = static_cast<const float*>(input);
        std::vector<float> buffer(samples, samples + frameCount);
        self->dataCallback(buffer);
    }
    return 0;
}
