#ifndef SPATIAL_PARIETAL_HPP
#define SPATIAL_PARIETAL_HPP

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CORTEZA PARIETAL POSTERIOR & COLÍCULO SUPERIOR (AoA & MAPA ESPACIAL)
// ============================================================================
// Biología: Modela la computación de diferencia interaural de fase (IPD)
// del núcleo olivar superior y la proyección retinotópica/somatotópica
// del Colículo Superior para fijación de objetivos en coordenadas espaciales.
// ============================================================================

struct SpatialTarget {
    double angle_deg = 0.0;     // Ángulo de llegada (AoA) en grados [-90, +90]
    double distance_m = 0.0;    // Distancia estimada en metros [0.5, 6.0]
    double x_m = 0.0;           // Coordenada X relativa en el cuarto
    double y_m = 0.0;           // Coordenada Y relativa en el cuarto
    double confidence = 0.0;    // Nivel de certeza de la posición [0, 1]
    bool target_present = false;// Si hay una perturbación espacial activa
};

class SpatialParietalCortex {
public:
    SpatialParietalCortex(int n_spatial_neurons = 16, double antenna_spacing_m = 0.08, double wavelength_m = 0.125)
        : n_neurons_(n_spatial_neurons),
          d_ant_(antenna_spacing_m),
          lambda_(wavelength_m),
          smooth_angle_(0.0),
          smooth_dist_(2.0),
          spatial_activity_(n_spatial_neurons, 0.0) {}

    // Procesa el vector de diferencias de fase entre antenas Rx1 y Rx2 o AoA directo
    SpatialTarget process_phase_interferometry(const std::vector<float>& phase_diff, 
                                             const std::vector<float>& amplitudes, 
                                             float doppler_centroid,
                                             float direct_aoa_deg = 0.0f,
                                             bool use_direct_aoa = false) {
        SpatialTarget target;

        double doppler_act = std::abs(static_cast<double>(doppler_centroid));
        double avg_amp = 0.0;
        if (!amplitudes.empty()) {
            for (float a : amplitudes) avg_amp += a;
            avg_amp /= amplitudes.size();
        }

        double estimated_angle_deg = smooth_angle_;
        if (use_direct_aoa) {
            estimated_angle_deg = direct_aoa_deg;
        } else if (!phase_diff.empty()) {
            double sum_sin = 0.0, sum_cos = 0.0, total_weight = 0.0;
            for (size_t i = 0; i < phase_diff.size(); ++i) {
                double weight = (i < amplitudes.size()) ? static_cast<double>(amplitudes[i]) : 1.0;
                double phi = static_cast<double>(phase_diff[i]);
                sum_sin += weight * std::sin(phi);
                sum_cos += weight * std::cos(phi);
                total_weight += weight;
            }
            if (total_weight > 1e-6) {
                double mean_phase_diff = std::atan2(sum_sin, sum_cos);
                double ratio = (mean_phase_diff * lambda_) / (2.0 * M_PI * d_ant_);
                ratio = std::max(-0.95, std::min(0.95, ratio));
                estimated_angle_deg = std::asin(ratio) * (180.0 / M_PI);
            }
        }

        // Estimación de distancia en Campo Cercano / Lejano (Near-Field Path Loss Model)
        double est_dist = 0.20 + 2.6 / (1.0 + 8.0 * avg_amp + 4.0 * doppler_act);
        est_dist = std::max(0.18, std::min(4.5, est_dist));

        bool motion_detected = (doppler_act > 0.06 || avg_amp > 0.35);
        target.target_present = (doppler_act > 0.04 || avg_amp > 0.18);

        // Memoria y retención de posición (EKF simplificado):
        // Si hay movimiento activo, actualiza el ángulo y la distancia dinámicamente.
        // Si el usuario se detiene/sienta, RETIENE la última posición fija en lugar de decaer al centro.
        if (motion_detected || !target_locked_) {
            smooth_angle_ += (estimated_angle_deg - smooth_angle_) * 0.30;
            smooth_dist_ += (est_dist - smooth_dist_) * 0.20;
            if (target.target_present) target_locked_ = true;
        } else if (!target.target_present) {
            // Si la sala está vacía de forma prolongada, relajación lenta a fondo
            smooth_dist_ += (3.5 - smooth_dist_) * 0.05;
            target_locked_ = false;
        }

        // Coordenadas cartesianas relativas en el plano horizontal
        double rad = smooth_angle_ * (M_PI / 180.0);
        target.angle_deg = smooth_angle_;
        target.distance_m = smooth_dist_;
        target.x_m = smooth_dist_ * std::sin(rad);
        target.y_m = smooth_dist_ * std::cos(rad);
        target.confidence = std::min(1.0, doppler_act * 0.5 + (avg_amp > 0.20 ? 0.7 : 0.1));

        // Activar sintonía de las 16 neuronas espaciales (Place/Grid fields)
        // Distribuidas angularmente desde -75° a +75°
        for (int i = 0; i < n_neurons_; ++i) {
            double pref_angle = -75.0 + (150.0 / (n_neurons_ - 1)) * i;
            double diff = smooth_angle_ - pref_angle;
            // Función de respuesta Gaussiana (Tuning curve)
            spatial_activity_[i] = target.target_present 
                ? std::exp(-(diff * diff) / (2.0 * 20.0 * 20.0)) * target.confidence * 40.0
                : 0.0;
        }

        last_target_ = target;
        return target;
    }

    const std::vector<double>& get_spatial_activity() const { return spatial_activity_; }
    const SpatialTarget& get_last_target() const { return last_target_; }

private:
    int n_neurons_;
    double d_ant_;
    double lambda_;
    double smooth_angle_;
    double smooth_dist_;
    bool target_locked_ = false;
    std::vector<double> spatial_activity_;
    SpatialTarget last_target_;
};

#endif // SPATIAL_PARIETAL_HPP
