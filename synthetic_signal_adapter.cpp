#include "synthetic_signal_adapter.hpp"
#include <sstream>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SyntheticSignalAdapter::SyntheticSignalAdapter(int num_channels, double noise_level)
    : num_channels_(num_channels),
      noise_level_(noise_level),
      connected_(false),
      phase_(0.0),
      current_state_(1),
      reward_(0.0),
      sample_counter_(0),
      rng_(42),
      dist_norm_(0.0, 1.0) {}

bool SyntheticSignalAdapter::connect() {
    connected_ = true;
    return true;
}

void SyntheticSignalAdapter::disconnect() {
    connected_ = false;
}

bool SyntheticSignalAdapter::is_connected() const {
    return connected_;
}

bool SyntheticSignalAdapter::read_sensory_frame(SensoryFrame& frame, double dt_sec, double time_ms) {
    if (!connected_) return false;

    sample_counter_++;
    phase_ += 2.0 * M_PI * 1.5 * dt_sec; // Oscilacion base de 1.5 Hz

    frame.channels.resize(128);
    frame.spectrum_amps.resize(64);
    frame.spatial_gradients.resize(63);

    // 1. Generar 64 canales espectrales
    for (int i = 0; i < 64; ++i) {
        double freq_weight = std::sin(phase_ + (i * 0.1));
        double pattern = 0.0;
        if (current_state_ == 1) {
            pattern = std::exp(-std::pow((i - 20) / 6.0, 2.0)) * 1.8; // Pico en canal 20
        } else if (current_state_ == 2) {
            pattern = std::exp(-std::pow((i - 45) / 8.0, 2.0)) * 1.8; // Pico en canal 45
        } else {
            pattern = 0.1; // Reposo
        }

        double val = std::max(0.0, 0.4 + pattern + 0.3 * freq_weight + noise_level_ * dist_norm_(rng_));
        frame.spectrum_amps[i] = static_cast<float>(val);
        frame.channels[i] = val * 12.0; // Inyectar corriente proporcional a SNN
    }

    // 2. Generar 63 canales de gradientes espaciales
    for (int i = 0; i < 63; ++i) {
        double grad = std::sin(phase_ * 0.5 + i * 0.15) * 0.8 + noise_level_ * dist_norm_(rng_) * 0.2;
        frame.spatial_gradients[i] = static_cast<float>(grad);
        frame.channels[64 + i] = std::abs(grad) * 10.0;
    }

    // 3. Canal de velocidad / cinematica (canal 127)
    frame.motion_velocity = 0.5 * std::cos(phase_ * 0.8) + 0.1 * dist_norm_(rng_);
    frame.channels[127] = std::abs(frame.motion_velocity) * 8.0;

    frame.signal_quality = 0.95;
    frame.source_type = SensorSourceType::SYNTHETIC_BENCHMARK;
    frame.source_name = "SYNTHETIC_SIGNAL_GENERATOR";
    frame.status_label = "STREAMING_ACTIVE";
    frame.primary_samples_count = static_cast<int>(sample_counter_);
    frame.secondary_samples_count = static_cast<int>(sample_counter_);

    // Conmutacion periodica de estado para prueba de plasticidad (cada 20s)
    if (fmod(time_ms, 20000.0) < 1000.0 && time_ms > 1000.0) {
        current_state_ = (current_state_ % 2) + 1;
        reward_ = 0.10; // Recompensa transitoria
    } else {
        reward_ = 0.0;
    }

    return true;
}

void SyntheticSignalAdapter::send_motor_feedback(const std::vector<double>& motor_firing_rates, double time_ms) {
    (void)motor_firing_rates;
    (void)time_ms;
}

std::string SyntheticSignalAdapter::get_telemetry_json() const {
    std::ostringstream oss;
    oss << "{\"synthetic_benchmark\":{"
        << "\"samples_generated\":" << sample_counter_ << ","
        << "\"pattern_state\":" << current_state_ << ","
        << "\"snr_db\":30.0"
        << "}}";
    return oss.str();
}

double SyntheticSignalAdapter::get_reward() {
    double r = reward_;
    reward_ = 0.0;
    return r;
}

bool SyntheticSignalAdapter::is_calibrating() const {
    return false;
}
