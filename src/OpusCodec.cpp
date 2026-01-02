#include "../include/OpusCodec.hpp"
#include <opus/opus.h>
#include <iostream>
#include <stdexcept>

OpusCodec::OpusCodec()
    : encoder(nullptr)
    , decoder(nullptr)
    , sampleRate(48000)
    , channels(1) {}

OpusCodec::~OpusCodec() {
    if (encoder) {
        opus_encoder_destroy(encoder);
    }
    if (decoder) {
        opus_decoder_destroy(decoder);
    }
}

bool OpusCodec::init(int sr, int ch) {
    sampleRate = sr;
    channels = ch;

    int error;
    encoder = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || !encoder) {
        std::cerr << "Failed to create Opus encoder: " << opus_strerror(error) << std::endl;
        return false;
    }

    decoder = opus_decoder_create(sampleRate, channels, &error);
    if (error != OPUS_OK || !decoder) {
        std::cerr << "Failed to create Opus decoder: " << opus_strerror(error) << std::endl;
        return false;
    }

    // Настройки для голоса
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));

    return true;
}

std::vector<unsigned char> OpusCodec::encode(const std::vector<float>& pcm) {
    if (!encoder) {
        throw std::runtime_error("Encoder not initialized");
    }

    if (pcm.empty()) {
        return {};
    }

    std::vector<unsigned char> encoded(4000); // Максимальный размер
    int frameSize = static_cast<int>(pcm.size() / channels);

    int bytes = opus_encode_float(encoder, pcm.data(), frameSize,
                                 encoded.data(), encoded.size());

    if (bytes < 0) {
        std::cerr << "Encode error: " << opus_strerror(bytes) << std::endl;
        return {};
    }

    encoded.resize(bytes);
    return encoded;
}

std::vector<float> OpusCodec::decode(const std::vector<unsigned char>& encoded) {
    if (!decoder) {
        throw std::runtime_error("Decoder not initialized");
    }

    if (encoded.empty()) {
        return {};
    }

    std::vector<float> pcm(960); // 40ms at 48kHz

    int samples = opus_decode_float(decoder, encoded.data(), encoded.size(),
                                   pcm.data(), pcm.size() / channels, 0);

    if (samples < 0) {
        std::cerr << "Decode error: " << opus_strerror(samples) << std::endl;
        return {};
    }

    pcm.resize(samples * channels);
    return pcm;
}
