#pragma once

#include <vector>

struct OpusEncoder;
struct OpusDecoder;

class OpusCodec {
public:
    OpusCodec();
    ~OpusCodec();

    bool init(int sampleRate = 48000, int channels = 1);
    std::vector<unsigned char> encode(const std::vector<float>& pcm);
    std::vector<float> decode(const std::vector<unsigned char>& encoded);

private:
    OpusEncoder* encoder;
    OpusDecoder* decoder;
    int sampleRate;
    int channels;
};
