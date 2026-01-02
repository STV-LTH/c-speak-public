#include "../include/VoiceProcessor.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

VoiceProcessor::VoiceProcessor(int sampleRate, int frameSize)
    : sampleRate_(sampleRate)
    , frameSize_(frameSize) {

    noiseSuppressor_ = std::make_unique<NoiseSuppressor>(sampleRate, frameSize);
    setMode(MODE_STANDARD);
}

void VoiceProcessor::setMode(ProcessingMode mode) {
    mode_ = mode;

    switch (mode) {
        case MODE_AGGRESSIVE:
            setNoiseReduction(20.0f);
            if (noiseSuppressor_) {
                noiseSuppressor_->setSmoothing(0.95f, 0.5f);
                noiseSuppressor_->setSuppressionType(NoiseSuppressor::WIENER);
            }
            break;
        case MODE_STANDARD:
            setNoiseReduction(15.0f);
            if (noiseSuppressor_) {
                noiseSuppressor_->setSmoothing(0.98f, 0.7f);
                noiseSuppressor_->setSuppressionType(NoiseSuppressor::MMSE);
            }
            break;
        case MODE_CONSERVATIVE:
            setNoiseReduction(10.0f);
            if (noiseSuppressor_) {
                noiseSuppressor_->setSmoothing(0.99f, 0.8f);
                noiseSuppressor_->setSuppressionType(NoiseSuppressor::SPECTRAL_GATING);
            }
            break;
        case MODE_AUTO:
            setNoiseReduction(12.0f);
            if (noiseSuppressor_) {
                noiseSuppressor_->setSmoothing(0.97f, 0.6f);
            }
            break;
    }
}

void VoiceProcessor::setNoiseReduction(float db) {
    if (noiseSuppressor_) {
        noiseSuppressor_->setReduction(db);
    }
}

void VoiceProcessor::setMinGain(float gain) {
    // Метод для совместимости, но minGain теперь в NoiseSuppressor
    // Можно добавить если нужно
}

void VoiceProcessor::calibrateNoise(const std::vector<float>& noiseSample) {
    if (noiseSuppressor_) {
        noiseSuppressor_->calibrateNoise(noiseSample);
    }
}

std::vector<float> VoiceProcessor::process(const std::vector<float>& frame) {
    if (frame.empty() || frame.size() != static_cast<size_t>(frameSize_)) {
        return frame;
    }

    std::vector<float> processed = frame;

    // 1. DC фильтр
    applyDcFilter(processed);

    // 2. Измерение входного уровня
    inputLevelDb_ = calculateRmsDb(processed);

    // 3. Подавление шума
    if (nsEnabled_ && noiseSuppressor_) {
        processed = noiseSuppressor_->process(processed);
    }

    // 4. Автогейн
    if (agcEnabled_) {
        applyAutoGain(processed);
    }

    // 5. Лимитер
    if (limiterEnabled_) {
        applyLimiter(processed);
    }

    // 6. Измерение выходного уровня
    outputLevelDb_ = calculateRmsDb(processed);
    peakLevelDb_ = std::max(peakLevelDb_, calculatePeakDb(processed));

    return processed;
}

void VoiceProcessor::applyDcFilter(std::vector<float>& frame) {
    for (float& sample : frame) {
        dcOffset_ = dcAlpha_ * dcOffset_ + (1.0f - dcAlpha_) * sample;
        sample -= dcOffset_;
    }
}

void VoiceProcessor::applyAutoGain(std::vector<float>& frame) {
    float currentDb = calculateRmsDb(frame);

    // Вычисляем желаемое усиление
    float desiredGainDb = targetLevelDb_ - currentDb;

    // Ограничиваем максимальное усиление
    desiredGainDb = std::clamp(desiredGainDb, minGainDb_, maxGainDb_);

    // Плавное изменение gain
    float targetGain = powf(10.0f, desiredGainDb / 20.0f);
    float alpha = (targetGain > currentGain_) ? 0.1f : 0.01f; // Разные скорости

    currentGain_ = alpha * targetGain + (1.0f - alpha) * currentGain_;

    // Применяем gain
    for (float& sample : frame) {
        sample *= currentGain_;
    }
}

void VoiceProcessor::applyLimiter(std::vector<float>& frame) {
    const float threshold = 0.9f;
    const float release = 0.999f;

    static float envelope = 0.0f;

    for (float& sample : frame) {
        float absSample = std::abs(sample);

        if (absSample > envelope) {
            envelope = absSample;
        } else {
            envelope = release * envelope + (1.0f - release) * absSample;
        }

        if (envelope > threshold) {
            float reduction = threshold / envelope;
            sample *= reduction;

            if (reduction < 0.99f) {
                clipCount_++;
            }
        }
    }
}

float VoiceProcessor::calculateRmsDb(const std::vector<float>& frame) const {
    if (frame.empty()) return MIN_DB;

    float sum = 0.0f;
    for (float sample : frame) {
        sum += sample * sample;
    }

    float rms = sqrtf(sum / frame.size());
    return 20.0f * log10f(rms + 1e-10f);
}

float VoiceProcessor::calculatePeakDb(const std::vector<float>& frame) const {
    if (frame.empty()) return MIN_DB;

    float peak = 0.0f;
    for (float sample : frame) {
        peak = std::max(peak, std::abs(sample));
    }

    return 20.0f * log10f(peak + 1e-10f);
}

VoiceProcessor::Stats VoiceProcessor::getStats() const {
    Stats stats;
    stats.inputLevelDb = inputLevelDb_;
    stats.outputLevelDb = outputLevelDb_;
    stats.noiseLevelDb = noiseSuppressor_ ? noiseSuppressor_->getNoiseLevelDb() : MIN_DB;
    stats.snrDb = noiseSuppressor_ ? noiseSuppressor_->getSnrDb() : 0.0f;
    stats.gainAppliedDb = 20.0f * log10f(currentGain_ + 1e-10f);
    stats.clipping = (clipCount_ > 0);

    return stats;
}
