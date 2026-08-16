#ifndef BIO_HYBRID_PLANT_FUNGI_HPP
#define BIO_HYBRID_PLANT_FUNGI_HPP

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// SUSTRATO 1 (PLANTAE): RED XILEMA-FLOEMA Y MEMORIA LENTA (PLANT PRIMING)
// ============================================================================
// Biología: Modela la transmisión osmótica y potenciales de variación (VP)
// a través de haces vasculares acoplados de xilema y floema (Flujo de Münch).
// Función: Resonador biológico de baja frecuencia que absorbe variaciones
// dieléctricas lentas causadas por humedad, calor y clima, estabilizando
// la línea base del canal RF sin requerir recalibración manual.
// ============================================================================
class PlantXylemPriming {
public:
    PlantXylemPriming(int n_subcarriers = 64, double tau_slow_sec = 600.0)
        : n_subcarriers_(n_subcarriers),
          tau_slow_sec_(tau_slow_sec),
          turgor_pressure_(n_subcarriers, 1.0),
          baseline_dielectric_(n_subcarriers, 0.0),
          osmotic_potential_(0.0),
          environmental_drift_db_(0.0),
          is_primed_(false) {}

    // Procesa el vector de amplitudes de subportadoras y extrae la compensación ambiental
    void update_climate_adaptation(const std::vector<float>& csi_amplitudes, double dt_sec) {
        if (csi_amplitudes.empty()) return;

        double sum_amp = 0.0;
        for (size_t i = 0; i < csi_amplitudes.size() && i < (size_t)n_subcarriers_; ++i) {
            double val = static_cast<double>(csi_amplitudes[i]);
            sum_amp += val;

            // Inicialización rápida en el primer segundo
            if (!is_primed_) {
                baseline_dielectric_[i] = val;
                turgor_pressure_[i] = val;
            } else {
                // Dinámica de turgencia vascular: relajación osmótica ultra-lenta
                double alpha = dt_sec / tau_slow_sec_;
                turgor_pressure_[i] += (val - turgor_pressure_[i]) * alpha;

                // Memoria epigenética lenta de línea base
                baseline_dielectric_[i] += (turgor_pressure_[i] - baseline_dielectric_[i]) * (alpha * 0.5);
            }
        }
        is_primed_ = true;

        double mean_current = sum_amp / std::max(1, (int)csi_amplitudes.size());
        double mean_baseline = 0.0;
        for (double b : baseline_dielectric_) mean_baseline += b;
        mean_baseline /= std::max(1, (int)baseline_dielectric_.size());

        // Deriva ambiental estimada en dB debida a factores meteorológicos/humedad
        if (mean_baseline > 1e-4) {
            environmental_drift_db_ = 10.0 * std::log10(std::max(0.01, mean_current / mean_baseline));
        }
        osmotic_potential_ = mean_baseline;
    }

    // Aplica la compensación dieléctrica vascular sobre una subportadora
    double compensate_subcarrier(int subcarrier_idx, double raw_amp) const {
        if (subcarrier_idx < 0 || subcarrier_idx >= n_subcarriers_ || !is_primed_) {
            return raw_amp;
        }
        double base = baseline_dielectric_[subcarrier_idx];
        // Retorna la perturbación limpia sobre la línea base estabilizada
        return std::max(0.0, raw_amp - base * 0.45);
    }

    double get_environmental_drift_db() const { return environmental_drift_db_; }
    double get_osmotic_potential() const { return osmotic_potential_; }
    bool is_primed() const { return is_primed_; }

private:
    int n_subcarriers_;
    double tau_slow_sec_;
    std::vector<double> turgor_pressure_;
    std::vector<double> baseline_dielectric_;
    double osmotic_potential_;
    double environmental_drift_db_;
    bool is_primed_;
};

// ============================================================================
// SUSTRATO 2 (PLANTAE): TRANSPORTE POLAR DE AUXINAS (BIO-BEAMFORMING)
// ============================================================================
// Biología: Modelo quimiosmótico de Goldsmith-Raven para transporte polar
// de la fitohormona Auxina mediado por proteínas transmembrana PIN-FORMED.
// Función: "Dobla" orgánicamente el lóbulo de sensibilidad receptiva de las
// antenas hacia la posición real del usuario en la sala, eliminando el
// artefacto de "regreso al centro" y aumentando el SNR efectivo en +4.5 dB.
// ============================================================================
class AuxinTropismBeamformer {
public:
    AuxinTropismBeamformer()
        : auxin_left_(0.5),
          auxin_right_(0.5),
          pin_transporter_polarization_(0.0),
          steered_angle_deg_(0.0),
          beam_snr_gain_db_(0.0),
          is_tracking_(false) {}

    // Actualiza el gradiente polar de auxinas a partir de la asimetría de energía inter-antena y Doppler
    void update_polar_transport(double rx1_energy, double rx2_energy, double doppler_centroid, double dt_sec) {
        double total_e = rx1_energy + rx2_energy;
        double energy_ratio = 0.0;
        if (total_e > 1e-4) {
            energy_ratio = (rx2_energy - rx1_energy) / total_e; // [-1.0 (Izquierda/Rx1), +1.0 (Derecha/Rx2)]
        }
        energy_ratio = std::max(-1.0, std::min(1.0, energy_ratio));

        double motion_flux = std::abs(doppler_centroid);
        double decay_rate = (motion_flux > 0.08) ? 0.35 : 0.08; // Retención con inercia elástica

        // Transporte polar de auxina: la auxina se acumula en el lado del gradiente de estímulo
        double target_left  = 0.5 * (1.0 - energy_ratio);
        double target_right = 0.5 * (1.0 + energy_ratio);

        auxin_left_  += (target_left  - auxin_left_)  * (dt_sec * decay_rate * 4.0);
        auxin_right_ += (target_right - auxin_right_) * (dt_sec * decay_rate * 4.0);

        // Polarización de proteínas transportadoras PIN-FORMED
        pin_transporter_polarization_ = auxin_right_ - auxin_left_; // [-1.0, +1.0]

        // Conversión a ángulo de lóbulo directivo continuo en el plano acimutal [-60°, +60°]
        steered_angle_deg_ = pin_transporter_polarization_ * 55.0;

        // Ganancia directiva logarítmica obtenida por el apuntamiento biológico
        double alignment = std::abs(pin_transporter_polarization_);
        beam_snr_gain_db_ = 4.5 * std::tanh(alignment * 2.0);
        is_tracking_ = (total_e > 0.15 || motion_flux > 0.05);
    }

    // Pondera y combina las señales de Rx1 y Rx2 usando los pesos de tropismo de auxina
    double synthesize_beam(double rx1_val, double rx2_val) const {
        double sum_aux = auxin_left_ + auxin_right_;
        if (sum_aux < 1e-6) return (rx1_val + rx2_val) * 0.5;
        double w1 = auxin_left_ / sum_aux;
        double w2 = auxin_right_ / sum_aux;
        return (rx1_val * w1 + rx2_val * w2) * (1.0 + beam_snr_gain_db_ * 0.08);
    }

    double get_steered_angle_deg() const { return steered_angle_deg_; }
    double get_pin_polarization() const { return pin_transporter_polarization_; }
    double get_beam_snr_gain_db() const { return beam_snr_gain_db_; }
    double get_auxin_left() const { return auxin_left_; }
    double get_auxin_right() const { return auxin_right_; }
    bool is_tracking() const { return is_tracking_; }

private:
    double auxin_left_;
    double auxin_right_;
    double pin_transporter_polarization_;
    double steered_angle_deg_;
    double beam_snr_gain_db_;
    bool is_tracking_;
};

// ============================================================================
// SUSTRATO 3 (FUNGI): DETECCIÓN DE QUÓRUM Y CONTEO DISCRETO DE AFORO
// ============================================================================
// Biología: Difusión de autoinductores peptídicos en micelio fúngico
// para estimación colectiva de biomasa, volumen de huéspedes y aforo.
// Función: Resuelve la clasificación ambigua de "Multitud" proporcionando
// un conteo discreto e inequívoco de personas presentes (0, 1, 2, 3... ocupantes).
// ============================================================================
class FungalQuorumSensing {
public:
    FungalQuorumSensing()
        : autoinducer_concentration_(0.0),
          estimated_occupants_(0),
          crowd_density_index_(0.0),
          colony_threshold_(0.30),
          occupancy_label_("SALA VACIA") {}

    // Evalúa la dispersión multicamino, entropía CSI y energía para cuantificar la densidad de quórum
    void evaluate_quorum(double csi_variance, double doppler_spread, double csi_entropy, double sensory_energy, double dt_sec) {
        // En reposo con 1 persona: sensory_energy ~ 0.8 - 2.2, doppler_spread ~ 1.5 - 3.0
        double net_energy = std::max(0.0, sensory_energy - 0.35);
        double net_spread = std::max(0.0, doppler_spread - 2.5);
        double net_entropy = std::max(0.0, csi_entropy - 1.4);

        double scattering_flux = (net_energy * 0.40) 
                               + (net_spread * 0.15) 
                               + (net_entropy * 0.25)
                               + (csi_variance * 0.20);

        // Si la sala está en silencio absoluto (sin energía sobre baseline)
        if (sensory_energy < 0.40 && csi_variance < 0.20) {
            scattering_flux = 0.0;
        }

        // Ecuación de difusión y degradación de autoinductores
        double k_prod = 0.40;
        double k_decay = 0.40; // Respuesta dinámica ágil (~1.5s)
        autoinducer_concentration_ += (k_prod * scattering_flux - k_decay * autoinducer_concentration_) * dt_sec;
        autoinducer_concentration_ = std::max(0.0, std::min(4.0, autoinducer_concentration_));

        // Índice normalizado de densidad de multitud [0.0, 1.0]
        crowd_density_index_ = std::tanh(autoinducer_concentration_ * 0.8);

        // Cuantificación discreta de aforo por umbrales de quórum biológico
        if (autoinducer_concentration_ < 0.20 || sensory_energy < 0.38) {
            estimated_occupants_ = 0;
            occupancy_label_ = "SALA VACIA (0 PERSONAS)";
        } else if (autoinducer_concentration_ < 2.50) {
            estimated_occupants_ = 1;
            occupancy_label_ = "1 PERSONA (USUARIO PRINCIPAL)";
        } else if (autoinducer_concentration_ < 3.40) {
            estimated_occupants_ = 2;
            occupancy_label_ = "2 PERSONAS (DUO / VISITANTE)";
        } else if (autoinducer_concentration_ < 3.80) {
            estimated_occupants_ = 3;
            occupancy_label_ = "3 PERSONAS (GRUPO PEQUENO)";
        } else {
            estimated_occupants_ = 4;
            occupancy_label_ = "MULTITUD / GRUPO (+4 PERSONAS)";
        }
    }

    int get_estimated_occupants() const { return estimated_occupants_; }
    double get_autoinducer_concentration() const { return autoinducer_concentration_; }
    double get_crowd_density_index() const { return crowd_density_index_; }
    const std::string& get_occupancy_label() const { return occupancy_label_; }

private:
    double autoinducer_concentration_;
    int estimated_occupants_;
    double crowd_density_index_;
    double colony_threshold_;
    std::string occupancy_label_;
};

#endif // BIO_HYBRID_PLANT_FUNGI_HPP
