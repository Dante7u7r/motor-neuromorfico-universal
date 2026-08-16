#ifndef CEREBELLAR_MODEL_HPP
#define CEREBELLAR_MODEL_HPP

#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// MODELO CEREBELOSO PREDICTIVO (CEREBELLUM FORWARD MODEL)
// ============================================================================
// Biología: Modela la microarquitectura de células de Purkinje, fibras musgosas,
// células granulares y fibras trepadoras (Climbing Fibers de la Oliva Inferior).
// Realiza predicción de estado en t+1 y cancelación adaptativa de eco estático.
// ============================================================================

class CerebellarPredictor {
public:
    CerebellarPredictor(int n_sensory = 128, int n_granule = 64, int n_purkinje = 30)
        : n_sensory_(n_sensory),
          n_granule_(n_granule),
          n_purkinje_(n_purkinje),
          learning_rate_(0.02),
          granule_weights_(n_granule, std::vector<double>(n_sensory, 0.1)),
          purkinje_weights_(n_purkinje, std::vector<double>(n_granule, 0.05)),
          clutter_baseline_(n_sensory, 0.0),
          predicted_sensory_(n_sensory, 0.0),
          sensory_error_(n_sensory, 0.0),
          purkinje_activity_(n_purkinje, 0.0) {
        
        // Inicializar pesos de expansión granular no lineal (Random Projection de Fibras Musgosas)
        for (int g = 0; g < n_granule_; ++g) {
            for (int s = 0; s < n_sensory_; ++s) {
                granule_weights_[g][s] = ((g + s) % 7 == 0) ? 0.45 : 0.05;
            }
        }
    }

    // Paso de integración hacia adelante del Cerebelo
    // Retorna la señal sensorial filtrada (libre de eco estático de fondo)
    std::vector<double> forward_and_adapt(const std::vector<double>& raw_sensory, double time_ms) {
        if (raw_sensory.size() < static_cast<size_t>(n_sensory_)) {
            return raw_sensory;
        }

        // 1. Actualización lenta del Clutter de Fondo (Eco estático de paredes y muebles)
        for (int s = 0; s < n_sensory_; ++s) {
            clutter_baseline_[s] += (raw_sensory[s] - clutter_baseline_[s]) * 0.005; // Constante de tiempo ~20s
        }

        // 2. Capa de Células Granulares (Expansión no lineal de dimensión)
        std::vector<double> granule_act(n_granule_, 0.0);
        for (int g = 0; g < n_granule_; ++g) {
            double net = 0.0;
            for (int s = 0; s < n_sensory_; ++s) {
                // Entrada = señal neta menos el clutter de fondo
                double dynamic_signal = raw_sensory[s] - clutter_baseline_[s];
                net += dynamic_signal * granule_weights_[g][s];
            }
            // Umbral de disparo granular no lineal (ReLU con umbral)
            granule_act[g] = std::max(0.0, net - 0.15);
        }

        // 3. Células de Purkinje (Salida inhibitoria del córtex cerebeloso)
        for (int p = 0; p < n_purkinje_; ++p) {
            double net = 0.0;
            for (int g = 0; g < n_granule_; ++g) {
                net += granule_act[g] * purkinje_weights_[p][g];
            }
            purkinje_activity_[p] = std::max(0.0, std::min(50.0, net));
        }

        // 4. Predicción del estado sensorial en t+1 por Núcleos Profundos (DCN)
        std::vector<double> filtered_sensory(n_sensory_, 0.0);
        for (int s = 0; s < n_sensory_; ++s) {
            int p_idx = s % n_purkinje_;
            // La señal predicha es generada por las células de Purkinje
            double purkinje_inhibition = purkinje_activity_[p_idx] * 0.2;
            double dynamic_component = std::max(0.0, raw_sensory[s] - clutter_baseline_[s]);
            
            // Señal filtrada: combina dinámica viva + corrección predictiva
            filtered_sensory[s] = std::max(0.0, dynamic_component + purkinje_inhibition);
            
            // Error de predicción sensorial (Oliva Inferior)
            sensory_error_[s] = raw_sensory[s] - (clutter_baseline_[s] + purkinje_inhibition);
        }

        // 5. Plasticidad Sináptica LTD/LTP cerebelosa (Heterosináptica dependiente del Error)
        for (int p = 0; p < n_purkinje_; ++p) {
            int s_ref = p * (n_sensory_ / n_purkinje_);
            double err = sensory_error_[s_ref];
            for (int g = 0; g < n_granule_; ++g) {
                if (granule_act[g] > 0.01) {
                    // Si hubo error positivo -> LTD (depresión a largo plazo de fibras paralelas)
                    purkinje_weights_[p][g] += learning_rate_ * err * granule_act[g] * 0.01;
                    purkinje_weights_[p][g] = std::max(0.001, std::min(1.5, purkinje_weights_[p][g]));
                }
            }
        }

        predicted_sensory_ = filtered_sensory;
        return filtered_sensory;
    }

    const std::vector<double>& get_purkinje_activity() const { return purkinje_activity_; }
    const std::vector<double>& get_clutter_baseline() const { return clutter_baseline_; }
    double get_mean_error() const {
        double sum = 0.0;
        for (double e : sensory_error_) sum += std::abs(e);
        return sum / sensory_error_.size();
    }

private:
    int n_sensory_;
    int n_granule_;
    int n_purkinje_;
    double learning_rate_;

    std::vector<std::vector<double>> granule_weights_;
    std::vector<std::vector<double>> purkinje_weights_;
    std::vector<double> clutter_baseline_;
    std::vector<double> predicted_sensory_;
    std::vector<double> sensory_error_;
    std::vector<double> purkinje_activity_;
};

#endif // CEREBELLAR_MODEL_HPP
