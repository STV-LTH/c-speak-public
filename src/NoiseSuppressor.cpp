#include "../include/NoiseSuppressor.hpp"
#include <iostream>
#include <numeric>

// Упрощенная FFT реализация
namespace {
    void bitReverse(std::vector<std::complex<float>>& data) {
        int n = data.size();
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;

            if (i < j) {
                std::swap(data[i], data[j]);
            }
        }
    }

    void iterativeFFT(std::vector<std::complex<float>>& data, bool inverse = false) {
        int n = data.size();
        if (n <= 1) return;

        bitReverse(data);

        for (int len = 2; len <= n; len <<= 1) {
            float angle = 2.0f * M_PI / len * (inverse ? 1.0f : -1.0f);
            std::complex<float> wlen(cosf(angle), sinf(angle));

            for (int i = 0; i < n; i += len) {
                std::complex<float> w(1.0f);
                for (int j = 0; j < len/2; ++j) {
                    std::complex<float> u = data[i + j];
                    std::complex<float> v = data[i + j + len/2] * w;
                    data[i + j] = u + v;
                    data[i + j + len/2] = u - v;
                    w *= wlen;
                }
            }
        }

        if (inverse) {
            for (auto& x : data) {
                x /= static_cast<float>(n);
            }
        }
    }
}

NoiseSuppressor::NoiseSuppressor(int sampleRate, int frameSize)
    : sampleRate_(sampleRate)
    , frameSize_(frameSize)
    , fftSize_(frameSize * 2)
    , numBins_(fftSize_ / 2 + 1)
    , overlapSize_(frameSize_ / 2) {

    // Окна
    analysisWindow_.resize(fftSize_);
    synthesisWindow_.resize(fftSize_);

    // Окно синус-квадрат
    for (int i = 0; i < fftSize_; ++i) {
        analysisWindow_[i] = sinf(M_PI * i / fftSize_);
        analysisWindow_[i] *= analysisWindow_[i];

        if (i < frameSize_) {
            synthesisWindow_[i] = analysisWindow_[i];
        }
    }

    // Нормализация
    float sum = 0.0f;
    for (int i = 0; i < overlapSize_; ++i) {
        sum += analysisWindow_[i] * synthesisWindow_[i] +
               analysisWindow_[i + overlapSize_] * synthesisWindow_[i + overlapSize_];
    }

    float scale = 2.0f / sum;
    for (int i = 0; i < fftSize_; ++i) {
        analysisWindow_[i] *= scale;
        synthesisWindow_[i] *= scale;
    }

    // Инициализация
    noiseEstimate_.resize(numBins_, 1e-6f);
    previousGains_.resize(numBins_, 1.0f);
    overlapBuffer_.resize(overlapSize_, 0.0f);

    std::cout << "NoiseSuppressor initialized" << std::endl;
}

void NoiseSuppressor::setReduction(float reductionDb) {
    reductionDb_ = std::min(std::max(reductionDb, 6.0f), 30.0f);
}

void NoiseSuppressor::setSmoothing(float timeSmoothing, float freqSmoothing) {
    timeSmoothing_ = std::clamp(timeSmoothing, 0.9f, 0.999f);
    freqSmoothing_ = std::clamp(freqSmoothing, 0.3f, 0.9f);
}

void NoiseSuppressor::calibrateNoise(const std::vector<float>& noiseFrame) {
    if (noiseFrame.size() != static_cast<size_t>(frameSize_)) return;

    std::vector<float> padded = noiseFrame;
    padded.resize(fftSize_, 0.0f);
    applyWindow(padded, true);

    std::vector<std::complex<float>> spectrum(fftSize_);
    for (size_t i = 0; i < padded.size(); ++i) {
        spectrum[i] = padded[i];
    }
    fft(spectrum);

    for (int i = 0; i < numBins_; ++i) {
        noiseEstimate_[i] = std::norm(spectrum[i]);
    }

    std::cout << "Noise calibration complete" << std::endl;
}

void NoiseSuppressor::fft(std::vector<std::complex<float>>& data, bool inverse) {
    iterativeFFT(data, inverse);
}

float NoiseSuppressor::estimateSNR(const std::complex<float>& bin, float noisePower) {
    float signalPower = std::norm(bin);
    return 10.0f * log10f(signalPower / (noisePower + 1e-10f) + 1e-10f);
}

std::vector<float> NoiseSuppressor::wienerFilter(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> gains(numBins_);

    for (int i = 0; i < numBins_; ++i) {
        float signalPower = std::norm(spectrum[i]);
        float noisePower = noiseEstimate_[i];

        float wienerGain = signalPower / (signalPower + noisePower + 1e-10f);
        float suppression = powf(10.0f, -reductionDb_ / 20.0f);
        wienerGain = std::max(wienerGain, suppression);

        gains[i] = sqrtf(wienerGain);
        gains[i] = std::max(gains[i], minGain_);
    }

    return gains;
}

std::vector<float> NoiseSuppressor::mmseFilter(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> gains(numBins_);

    for (int i = 0; i < numBins_; ++i) {
        float signalPower = std::norm(spectrum[i]);
        float noisePower = noiseEstimate_[i];

        // Упрощенный MMSE (без сложных вычислений)
        float snr = signalPower / (noisePower + 1e-10f);
        float mmseGain = snr / (1.0f + snr);

        // Применяем reduction
        float suppression = powf(10.0f, -reductionDb_ / 20.0f);
        mmseGain = std::max(mmseGain, suppression);

        gains[i] = sqrtf(mmseGain);
        gains[i] = std::max(gains[i], 0.1f); // Сохраняем хоть что-то
    }

    return gains;
}

std::vector<float> NoiseSuppressor::spectralGating(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> gains(numBins_);

    for (int i = 0; i < numBins_; ++i) {
        float magnitude = std::abs(spectrum[i]);
        float noiseMag = sqrtf(noiseEstimate_[i]);

        float threshold = noiseMag * powf(10.0f, reductionDb_ / 20.0f);

        if (magnitude < threshold) {
            float attenuation = magnitude / (threshold + 1e-10f);
            // Кубическая интерполяция для плавности
            attenuation = attenuation * attenuation * (3.0f - 2.0f * attenuation);
            gains[i] = attenuation;
        } else {
            gains[i] = 1.0f;
        }

        gains[i] = std::max(gains[i], 0.05f); // Очень мягкое минимальное значение
    }

    return gains;
}

void NoiseSuppressor::applySmoothing(std::vector<float>& gains) {
    // Сглаживание по частоте
    if (numBins_ > 2) {
        std::vector<float> smoothed = gains;
        for (int i = 1; i < numBins_ - 1; ++i) {
            smoothed[i] = (gains[i-1] + gains[i] + gains[i+1]) / 3.0f;
        }

        for (int i = 0; i < numBins_; ++i) {
            gains[i] = freqSmoothing_ * smoothed[i] + (1.0f - freqSmoothing_) * gains[i];
        }
    }

    // Сглаживание по времени
    for (int i = 0; i < numBins_; ++i) {
        gains[i] = timeSmoothing_ * previousGains_[i] +
                  (1.0f - timeSmoothing_) * gains[i];
        previousGains_[i] = gains[i];
    }
}

void NoiseSuppressor::applyWindow(std::vector<float>& data, bool analysis) {
    const std::vector<float>& window = analysis ? analysisWindow_ : synthesisWindow_;
    for (size_t i = 0; i < data.size() && i < window.size(); ++i) {
        data[i] *= window[i];
    }
}

std::vector<float> NoiseSuppressor::process(const std::vector<float>& frame) {
    if (frame.size() != static_cast<size_t>(frameSize_)) return frame;

    // 1. Подготовка
    std::vector<float> padded = frame;
    padded.resize(fftSize_, 0.0f);
    applyWindow(padded, true);

    // 2. FFT
    std::vector<std::complex<float>> spectrum(fftSize_);
    for (size_t i = 0; i < padded.size(); ++i) {
        spectrum[i] = padded[i];
    }
    fft(spectrum);

    // 3. Выбор фильтра
    std::vector<float> gains;
    switch (suppressionType_) {
        case WIENER:
            gains = wienerFilter(spectrum);
            break;
        case MMSE:
            gains = mmseFilter(spectrum);
            break;
        case SPECTRAL_GATING:
            gains = spectralGating(spectrum);
            break;
        default:
            gains.resize(numBins_, 1.0f);
            break;
    }

    // 4. Сглаживание
    applySmoothing(gains);

    // 5. Применение gain
    for (int i = 0; i < numBins_; ++i) {
        spectrum[i] *= gains[i];
    }

    // Симметрия для реального сигнала
    for (int i = 1; i < numBins_ - 1; ++i) {
        spectrum[fftSize_ - i] = std::conj(spectrum[i]);
    }

    // 6. Обратное FFT
    fft(spectrum, true);

    // 7. Извлечение реальной части
    std::vector<float> processed(fftSize_);
    for (int i = 0; i < fftSize_; ++i) {
        processed[i] = spectrum[i].real();
    }
    applyWindow(processed, false);

    // 8. Overlap-add
    std::vector<float> output(frameSize_, 0.0f);

    for (int i = 0; i < frameSize_; ++i) {
        if (i < overlapSize_) {
            output[i] = overlapBuffer_[i] + processed[i];
        } else {
            output[i] = processed[i];
        }
    }

    // 9. Сохранение overlap
    for (int i = 0; i < overlapSize_; ++i) {
        overlapBuffer_[i] = processed[i + frameSize_ - overlapSize_];
    }

    // 10. Статистика
    float totalSignal = 0.0f, totalNoise = 0.0f;
    for (int i = 0; i < numBins_; ++i) {
        totalSignal += std::norm(spectrum[i]);
        totalNoise += noiseEstimate_[i];
    }

    noiseLevelDb_ = 10.0f * log10f(totalNoise / numBins_ + 1e-10f);
    float signalLevelDb = 10.0f * log10f(totalSignal / numBins_ + 1e-10f);
    snrDb_ = signalLevelDb - noiseLevelDb_;

    return output;
}
