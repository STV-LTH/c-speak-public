#pragma once

#include "NoiseSuppressor.hpp"
#include <vector>
#include <memory>

class VoiceProcessor {
public:
    enum ProcessingMode {
        MODE_AGGRESSIVE,
        MODE_STANDARD,
        MODE_CONSERVATIVE,
        MODE_AUTO
    };

    VoiceProcessor(int sampleRate = 48000, int frameSize = 960);

    // Обработка
    std::vector<float> process(const std::vector<float>& frame);

    // Настройки
    void setMode(ProcessingMode mode);
    void enableNoiseSuppression(bool enable) { nsEnabled_ = enable; }
    void enableAutoGain(bool enable) { agcEnabled_ = enable; }
    void enableLimiter(bool enable) { limiterEnabled_ = enable; }

    void setTargetLevel(float db) { targetLevelDb_ = db; }
    void setNoiseReduction(float db);
    void setMinGain(float gain);

    // Калибровка
    void calibrateNoise(const std::vector<float>& noiseSample);

    // Статистика
    struct Stats {
        float inputLevelDb;
        float outputLevelDb;
        float noiseLevelDb;
        float snrDb;
        float gainAppliedDb;
        bool clipping;
    };

    Stats getStats() const;

private:
    // Эффекты
    void applyAutoGain(std::vector<float>& frame);
    void applyLimiter(std::vector<float>& frame);
    void applyDcFilter(std::vector<float>& frame);

    // Вспомогательные
    float calculateRmsDb(const std::vector<float>& frame) const;
    float calculatePeakDb(const std::vector<float>& frame) const;

private:
    int sampleRate_;
    int frameSize_;

    // Компоненты
    std::unique_ptr<NoiseSuppressor> noiseSuppressor_;

    // Настройки
    ProcessingMode mode_ = MODE_STANDARD;
    bool nsEnabled_ = true;
    bool agcEnabled_ = true;
    bool limiterEnabled_ = true;

    float targetLevelDb_ = -18.0f;
    float maxGainDb_ = 20.0f;
    float minGainDb_ = -10.0f;
    float currentGain_ = 1.0f;

    // Состояние
    float inputLevelDb_ = -100.0f;
    float outputLevelDb_ = -100.0f;
    float peakLevelDb_ = -100.0f;

    // DC фильтр
    float dcOffset_ = 0.0f;
    float dcAlpha_ = 0.995f;

    // Статистика
    size_t clipCount_ = 0;

    static constexpr float MIN_DB = -100.0f;
};
