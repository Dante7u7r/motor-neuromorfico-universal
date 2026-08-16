#ifndef BASAL_GANGLIA_HPP
#define BASAL_GANGLIA_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

// ============================================================================
// GANGLIOS BASALES (BASAL GANGLIA: STRIATUM D1/D2, STN, GPi/SNr)
// ============================================================================
// Biología: Modela la selección de acción y resolución de competencia cortico-
// estriatal (Albin-Delong). Utiliza la vía directa (Go/D1) para facilitar
// la clasificación ganadora y la vía indirecta (No-Go/D2) para inhibir
// activamente las alternativas perdedoras.
// ============================================================================

struct BasalGangliaDecision {
    int selected_action = 0;        // 0: Vacío, 1: Sujeto A, 2: Sujeto B, 3: Multitud
    std::string label = "VACIO";
    double confidence = 0.0;
    std::vector<double> action_values; // Salida neta por cada canal
    std::vector<double> direct_pathway;   // Go (D1)
    std::vector<double> indirect_pathway; // No-Go (D2)
};

class BasalGangliaCircuit {
public:
    BasalGangliaCircuit(int n_actions = 4)
        : n_actions_(n_actions),
          labels_{"VACIO", "SUJETO_A", "SUJETO_B", "MULTITUD"},
          striatum_d1_(n_actions, 0.0),
          striatum_d2_(n_actions, 0.0),
          gpi_output_(n_actions, 1.0),
          action_values_(n_actions, 0.0),
          threshold_(0.35) {}

    // Procesa las entradas corticales (PFC, Oculta, Sensorial) y la dopamina fásica
    BasalGangliaDecision decide(const std::vector<double>& cortical_inputs, double dopamine_level) {
        BasalGangliaDecision decision;
        decision.action_values.resize(n_actions_, 0.0);
        decision.direct_pathway.resize(n_actions_, 0.0);
        decision.indirect_pathway.resize(n_actions_, 0.0);

        // 1. Integrar inputs corticales en canales de acción (4 candidatos)
        // cortical_inputs mapea:
        // canal 0 (Vacío): baja energía / sin Doppler
        // canal 1 (Sujeto A): alta similaridad genuina o patrón A
        // canal 2 (Sujeto B): alta similaridad B o cinemática B
        // canal 3 (Multitud): Doppler alto y entropía alta
        std::vector<double> raw_channels(n_actions_, 0.0);
        for (size_t i = 0; i < cortical_inputs.size(); ++i) {
            int ch = i % n_actions_;
            raw_channels[ch] += cortical_inputs[i];
        }

        // 2. Modulación Dopaminérgica de las Vías D1 (Go) y D2 (No-Go)
        // Dopamina alta (> 0.5) promueve Go (D1) y apaga No-Go (D2)
        // Dopamina baja (< 0.5) apaga Go y activa No-Go (inhibe acción)
        double da_factor_d1 = std::max(0.1, dopamine_level * 2.0);
        double da_factor_d2 = std::max(0.1, (1.0 - dopamine_level) * 2.0);

        for (int a = 0; a < n_actions_; ++a) {
            // Vía Directa (Striatum D1 -> Inhibe GPi -> Desinhibe Tálamo Motor -> GO)
            striatum_d1_[a] += (raw_channels[a] * da_factor_d1 - striatum_d1_[a]) * 0.4;
            
            // Vía Indirecta (Striatum D2 -> Inhibe GPe -> Excita STN -> Excita GPi -> Frena Tálamo -> NO-GO)
            striatum_d2_[a] += (raw_channels[a] * da_factor_d2 - striatum_d2_[a]) * 0.4;

            // Salida de Globus Pallidus Interno (GPi): tónicamente activo (freno)
            // GPi disminuye con D1 (quita el freno) y aumenta con D2 (pisa el freno)
            gpi_output_[a] = std::max(0.0, 1.0 - (striatum_d1_[a] * 0.05) + (striatum_d2_[a] * 0.03));
            
            // Valor de acción desinhibido en el Tálamo Motor:
            // Actividad tálamo-cortical = 1.0 - GPi
            action_values_[a] = std::max(0.0, 1.0 - gpi_output_[a]);
        }

        // 3. Winner-Take-All lateral estricto (Redes recurrentes del Subtálamo / GPi)
        int best_action = 0;
        double max_val = -1.0;
        double sum_val = 0.0;

        for (int a = 0; a < n_actions_; ++a) {
            sum_val += action_values_[a];
            if (action_values_[a] > max_val) {
                max_val = action_values_[a];
                best_action = a;
            }
        }

        // Si la señal es muy débil, la decisión por defecto biológica es VACÍO (canal 0)
        if (max_val < threshold_) {
            best_action = 0;
        }

        decision.selected_action = best_action;
        decision.label = (best_action < (int)labels_.size()) ? labels_[best_action] : "UNKNOWN";
        decision.confidence = (sum_val > 1e-4) ? std::min(1.0, max_val / sum_val) : 0.0;
        decision.action_values = action_values_;
        decision.direct_pathway = striatum_d1_;
        decision.indirect_pathway = striatum_d2_;

        last_decision_ = decision;
        return decision;
    }

    const BasalGangliaDecision& get_last_decision() const { return last_decision_; }

    // Aplica corrientes de inhibición cruzada directa a la capa motora
    void apply_motor_gating(std::vector<double>& motor_currents, int n_motor_neurons = 30) {
        if (motor_currents.size() < static_cast<size_t>(n_motor_neurons)) return;

        // Distribución de neuronas motoras:
        // Z0 (0..7): Vacío
        // Z1 (8..15): Sujeto A
        // Z2 (16..23): Sujeto B
        // Z3 (24..29): Multitud
        int action = last_decision_.selected_action;
        int zone_starts[4] = {0, 8, 16, 24};
        int zone_ends[4] = {8, 16, 24, n_motor_neurons};

        for (int z = 0; z < 4; ++z) {
            bool is_winner = (z == action);
            double gain = is_winner ? (1.0 + last_decision_.confidence * 2.0) : 0.05; // Supresión lateral del 95%
            for (int i = zone_starts[z]; i < zone_ends[z]; ++i) {
                motor_currents[i] *= gain;
                if (!is_winner) {
                    motor_currents[i] -= 10.0; // Inhibición activa No-Go
                }
            }
        }
    }

private:
    int n_actions_;
    std::vector<std::string> labels_;
    std::vector<double> striatum_d1_;
    std::vector<double> striatum_d2_;
    std::vector<double> gpi_output_;
    std::vector<double> action_values_;
    double threshold_;
    BasalGangliaDecision last_decision_;
};

#endif // BASAL_GANGLIA_HPP
