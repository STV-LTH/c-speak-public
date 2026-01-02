#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <memory>

class NoiseSuppressor {
public:
    enum SuppressionType {
        SUBTRACTION,    // Классическое вычитание
        WIENER,         // Винеровский фильтр
        MMSE,           // Minimum Mean-Square Error
        SPECTRAL_GATING // Пороговое подавление
    };

    NoiseSuppressor(int sampleRate = 48000, int frameSize = 960);
    ~NoiseSuppressor() = default;

    // Обработка
    std::vector<float> process(const std::vector<float>& frame);

    // Настройки
    void setSuppressionType(SuppressionType type) { suppressionType_ = type; }
    void setReduction(float reductionDb);
    void setSmoothing(float timeSmoothing, float freqSmoothing);

    // Калибровка
    void calibrateNoise(const std::vector<float>& noiseFrame);

    // Получение статистики
    float getNoiseLevelDb() const { return noiseLevelDb_; }
    float getSnrDb() const { return snrDb_; }

private:
    // Методы подавления
    std::vector<float> wienerFilter(const std::vector<std::complex<float>>& spectrum);
    std::vector<float> mmseFilter(const std::vector<std::complex<float>>& spectrum);
    std::vector<float> spectralGating(const std::vector<std::complex<float>>& spectrum);

    // FFT операции
    void fft(std::vector<std::complex<float>>& data, bool inverse = false);

    // Вспомогательные
    void applyWindow(std::vector<float>& data, bool analysis = true);
    void applySmoothing(std::vector<float>& gains);
    float estimateSNR(const std::complex<float>& bin, float noisePower);

private:
    int sampleRate_;
    int frameSize_;
    int fftSize_;
    int numBins_;

    // Настройки
    SuppressionType suppressionType_ = MMSE;
    float reductionDb_ = 15.0f;
    float timeSmoothing_ = 0.98f;
    float freqSmoothing_ = 0.7f;
    float minGain_ = 0.1f;

    // Окна
    std::vector<float> analysisWindow_;
    std::vector<float> synthesisWindow_;

    // Состояние
    std::vector<float> noiseEstimate_;
    std::vector<float> previousGains_;

    // Overlap-add
    std::vector<float> overlapBuffer_;
    int overlapSize_;

    // Статистика
    float noiseLevelDb_ = -100.0f;
    float snrDb_ = 0.0f;
};
