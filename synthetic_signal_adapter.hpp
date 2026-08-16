#ifndef SYNTHETIC_SIGNAL_ADAPTER_HPP
#define SYNTHETIC_SIGNAL_ADAPTER_HPP

#include "sensor_adapter.hpp"
#include <vector>
#include <string>
#include <random>
#include <cmath>

// Adaptador universal para generación sintética de señales biofísicas y benchmarks
class SyntheticSignalAdapter : public ISensorAdapter {
public:
    SyntheticSignalAdapter(int num_channels = 128, double noise_level = 0.1);
    ~SyntheticSignalAdapter() override = default;

    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    bool read_sensory_frame(SensoryFrame& frame, double dt_sec, double time_ms) override;
    void send_motor_feedback(const std::vector<double>& motor_firing_rates, double time_ms) override;

    std::string get_telemetry_json() const override;
    std::string get_source_name() const override { return "SYNTHETIC_SIGNAL_GENERATOR"; }
    SensorSourceType get_source_type() const override { return SensorSourceType::SYNTHETIC_BENCHMARK; }

    double get_reward() override;
    bool is_calibrating() const override;

    void set_target_state(int state_id) { current_state_ = state_id; }

private:
    int num_channels_;
    double noise_level_;
    bool connected_;
    double phase_;
    int current_state_; // 0=Silence/Rest, 1=Active Pattern A, 2=Active Pattern B
    double reward_;
    uint64_t sample_counter_;
    std::mt19937 rng_;
    std::normal_distribution<double> dist_norm_;
};

#endif // SYNTHETIC_SIGNAL_ADAPTER_HPP
