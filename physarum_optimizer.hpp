#ifndef PHYSARUM_OPTIMIZER_HPP
#define PHYSARUM_OPTIMIZER_HPP

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <string>
#include <map>

// ============================================================================
// OPTIMIZADOR BIOLÓGICO MULTI-PERSPECTIVA: MOHO MUCILAGINOSO (PHYSARUM POLYCEPHALUM)
// ============================================================================
// Biología: Modela la dinámica de red protoplásmica (Tero-Kobayashi-Nakagaki 2007)
// extendida para enrutamiento auto-organizado, digestión de ruido (Garbage Collection),
// diferenciación multiclase (Humano / Mascota / Vacío) y plasticidad morfogénica.
// ============================================================================

struct PhysarumTubule {
    int node_a;             // Neurona origen
    int node_b;             // Neurona destino
    double conductivity;    // Diámetro / Conductividad hidráulica D_ij [0.01, 3.0]
    double length;          // Distancia euclidiana 3D L_ij
    double flux;            // Flujo citoplasmático neto Q_ij
    double age_steps;       // Tiempo de vida del túbulo
    bool is_active;         // Si el túbulo sigue vivo o fue digerido/podado

    PhysarumTubule() : node_a(-1), node_b(-1), conductivity(0.2), length(1.0), 
                      flux(0.0), age_steps(0.0), is_active(true) {}
};

struct PhysarumTargetSource {
    int target_id;          // 0: Vacío, 1: Humano A, 2: Humano B, 3: Mascota, 4: Multitud
    std::string name;
    double chemo_attractant;// Concentración de alimento/error [0, 1]
    int motor_start_idx;    // Índice inicial en la capa motora
    int motor_end_idx;      // Índice final en la capa motora
};

class PhysarumOptimizer {
public:
    PhysarumOptimizer(int n_total_neurons = 274, double decay_gamma = 0.08, double growth_alpha = 0.25)
        : n_nodes_(n_total_neurons),
          gamma_(decay_gamma),
          alpha_(growth_alpha),
          total_biomass_(100.0),
          digested_waste_energy_(0.0),
          current_winning_target_(0) {
        
        pressure_.assign(n_total_neurons, 0.0);
        sources_.assign(n_total_neurons, 0.0);

        // Inicializar fuentes de alimento / atractantes químicos
        targets_ = {
            {0, "VACIO",     0.0, 214, 220}, // Z0: Vacío (Motor 214..220)
            {1, "HUMANO_A",  0.0, 221, 227}, // Z1: Humano Principal (Motor 221..227)
            {2, "HUMANO_B",  0.0, 228, 234}, // Z2: Humano Alternativo (Motor 228..234)
            {3, "MASCOTA",   0.0, 235, 240}, // Z3: Mascota / Micro-Doppler (Motor 235..240)
            {4, "MULTITUD",  0.0, 241, 243}  // Z4: Multitud / Alta Dinámica (Motor 241..243)
        };
    }

    // Inicializar túbulos a partir de la geometría 3D de las neuronas
    void build_initial_mesh(const std::vector<double>& node_x, 
                           const std::vector<double>& node_y, 
                           const std::vector<double>& node_z,
                           const std::vector<std::pair<int, int>>& initial_edges) {
        tubules_.clear();
        adjacency_.assign(n_nodes_, {});

        for (const auto& edge : initial_edges) {
            int u = edge.first;
            int v = edge.second;
            if (u < 0 || u >= n_nodes_ || v < 0 || v >= n_nodes_ || u == v) continue;

            double dx = node_x[u] - node_x[v];
            double dy = node_y[u] - node_y[v];
            double dz = node_z[u] - node_z[v];
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 1.0) dist = 1.0;

            PhysarumTubule tub;
            tub.node_a = u;
            tub.node_b = v;
            tub.length = dist;
            tub.conductivity = 0.25; // Grosor inicial homogéneo
            tub.flux = 0.0;
            tub.age_steps = 0.0;
            tub.is_active = true;

            int tub_idx = static_cast<int>(tubules_.size());
            tubules_.push_back(tub);
            adjacency_[u].push_back(tub_idx);
            adjacency_[v].push_back(tub_idx);
        }
    }

    // Paso de evolución del Moho: Actualiza fuentes químicas, presión hidrodinámica y flujo
    void step_evolution(const std::vector<double>& sensory_currents,
                        double doppler_centroid,
                        double doppler_spread,
                        double csi_entropy,
                        double siamese_genuine_similarity) {
        if (tubules_.empty()) return;

        // 1. EVALUAR PERSPECTIVAS Y CONFIGURAR ATRACTANTES QUÍMICOS (ALIMENTO)
        // Calcular energía sensorial neta
        double sensory_energy = 0.0;
        for (int i = 0; i < 64 && i < (int)sensory_currents.size(); ++i) {
            sensory_energy += sensory_currents[i];
        }
        sensory_energy /= 64.0;

        double abs_doppler = std::abs(doppler_centroid);

        // Estimación de envolvente respiratoria diafragmática (Anti-Falso Vacío)
        resp_history_.push_back(sensory_energy);
        if (resp_history_.size() > 60) resp_history_.pop_front();
        
        double resp_variance = 0.0;
        if (resp_history_.size() >= 20) {
            double mean_r = 0.0;
            for (double r : resp_history_) mean_r += r;
            mean_r /= resp_history_.size();
            for (double r : resp_history_) resp_variance += (r - mean_r) * (r - mean_r);
            resp_variance /= resp_history_.size();
        }
        bool has_breathing_pattern = (resp_variance > 0.002 && sensory_energy > 0.45);

        // --- INTEGRACIÓN TEMPORAL CON HISTÉRESIS BIOLÓGICA (Modelo Wilson-Cowan) ---
        // Evidencia dinámica instantánea de presencia humana (energía sobre suelo basal + micro-Doppler + respiración)
        double dynamic_evidence = std::max(0.0, (sensory_energy - 0.40) / 2.0) 
                                + std::min(0.6, abs_doppler * 0.35)
                                + (has_breathing_pattern ? 0.35 : 0.0);
        
        // Constantes asimétricas de integración (Potenciación rápida: 300ms | Persistencia biológica: 3500ms)
        double dt_sec = 0.05; // 50 ms de tick
        double tau = (dynamic_evidence > presence_accumulator_) ? 0.30 : 3.50;
        double alpha_dt = dt_sec / tau;
        presence_accumulator_ += (dynamic_evidence - presence_accumulator_) * alpha_dt;
        presence_accumulator_ = std::max(0.0, std::min(1.0, presence_accumulator_));

        // Biestabilidad Schmitt-Trigger (Evita parpadeo entre Vacío y Presencia)
        if (!is_presence_active_ && (presence_accumulator_ > 0.20 || has_breathing_pattern)) {
            is_presence_active_ = true;
        } else if (is_presence_active_ && presence_accumulator_ < 0.05 && !has_breathing_pattern) {
            is_presence_active_ = false;
        }

        // A. Vacío: La persistencia biológica determinó ausencia total de presencia
        if (!is_presence_active_) {
            targets_[0].chemo_attractant = 1.0;
            targets_[1].chemo_attractant = 0.01;
            targets_[2].chemo_attractant = 0.01;
            targets_[3].chemo_attractant = 0.01;
            targets_[4].chemo_attractant = 0.01;
        } else {
            targets_[0].chemo_attractant = 0.02;

            // Gating de Sección de Radar (RCS) y masa corporal
            bool has_torso_mass = (sensory_energy >= 0.85 || presence_accumulator_ > 0.30 || has_breathing_pattern);
            bool is_typing_motion = (abs_doppler >= 0.10 && abs_doppler <= 22.0);

            // B. Humano A (Sujeto Autorizado): Masa de torso + alta similitud siamesa G
            if (has_torso_mass && siamese_genuine_similarity >= 0.28) {
                targets_[1].chemo_attractant = 0.80 + 0.20 * siamese_genuine_similarity;
            } else if (has_torso_mass && is_typing_motion && siamese_genuine_similarity > 0.15) {
                targets_[1].chemo_attractant = 0.88;
            } else if (has_torso_mass) {
                targets_[1].chemo_attractant = 0.78;
            } else {
                targets_[1].chemo_attractant = 0.05;
            }

            // C. Humano B (Visitante): Masa corporal de torso pero perfil biométrico no coincidente (G baja)
            targets_[2].chemo_attractant = (has_torso_mass && siamese_genuine_similarity < 0.25 && is_typing_motion) ? 0.72 : 0.04;

            // D. Mascota (Cuadrúpedo): Masa corporal reducida (< 0.8) + cadencia de 4 patas (3.5 - 9.0 Hz)
            targets_[3].chemo_attractant = (!has_torso_mass && sensory_energy >= 0.6 && abs_doppler >= 3.5 && abs_doppler <= 9.0) ? 0.88 : 0.02;

            // E. Multitud: Dispersión Doppler amplia (> 18 Hz)
            targets_[4].chemo_attractant = (doppler_spread > 18.0) ? 0.90 : 0.02;
        }

        // Identificar el objetivo ganador
        int best_target = 0;
        double max_attract = -1.0;
        for (size_t t = 0; t < targets_.size(); ++t) {
            if (targets_[t].chemo_attractant > max_attract) {
                max_attract = targets_[t].chemo_attractant;
                best_target = static_cast<int>(t);
            }
        }
        current_winning_target_ = best_target;

        // 2. CONFIGURAR VECTOR DE FUENTES (S_i)
        // Nodos sensoriales (0..127) = Fuentes de presión positiva (+S)
        // Nodos motores del target ganador = Sumideros de presión negativa (-S, "Comida")
        std::fill(sources_.begin(), sources_.end(), 0.0);
        double total_inflow = 0.0;

        for (int i = 0; i < 128 && i < n_nodes_; ++i) {
            double source_strength = (i < (int)sensory_currents.size()) ? (sensory_currents[i] * 0.1) : 0.5;
            sources_[i] = source_strength;
            total_inflow += source_strength;
        }

        // Distribuir el sumidero (-S) equitativamente en la zona motora del objetivo ganador
        const auto& target = targets_[best_target];
        int n_target_nodes = target.motor_end_idx - target.motor_start_idx + 1;
        if (n_target_nodes > 0 && total_inflow > 1e-4) {
            double sink_per_node = -total_inflow / n_target_nodes;
            for (int k = target.motor_start_idx; k <= target.motor_end_idx && k < n_nodes_; ++k) {
                sources_[k] = sink_per_node;
            }
        }

        // 3. RESOLVER EL CAMPO DE PRESIÓN HIDRODINÁMICA (Ecuación de Poisson / Gauss-Seidel)
        // \sum_j (D_ij / L_ij) * (P_i - P_j) = S_i
        for (int iter = 0; iter < 12; ++iter) {
            for (int i = 0; i < n_nodes_; ++i) {
                double cond_sum = 0.0;
                double neighbor_sum = 0.0;

                for (int tub_idx : adjacency_[i]) {
                    const auto& tub = tubules_[tub_idx];
                    if (!tub.is_active) continue;

                    int j = (tub.node_a == i) ? tub.node_b : tub.node_a;
                    double conductance = tub.conductivity / tub.length;
                    cond_sum += conductance;
                    neighbor_sum += conductance * pressure_[j];
                }

                if (cond_sum > 1e-6) {
                    // Actualización relajada de Jacobi/Gauss-Seidel
                    pressure_[i] = (neighbor_sum + sources_[i]) / cond_sum;
                }
            }
        }

        // 4. CALCULAR FLUJOS CITOPLÁSMICOS Y ADAPTAR DIÁMETROS D_ij (Morfogénesis)
        // dD_ij/dt = alpha * |Q_ij|^1.1 - gamma * D_ij
        digested_waste_energy_ = 0.0;
        int active_tubules_count = 0;

        for (auto& tub : tubules_) {
            if (!tub.is_active) continue;

            // Ley de Poiseuille: Q_ij = (D_ij / L_ij) * (P_a - P_b)
            double dp = pressure_[tub.node_a] - pressure_[tub.node_b];
            tub.flux = (tub.conductivity / tub.length) * dp;
            double abs_flux = std::abs(tub.flux);

            // Reforzar si transporta nutrientes/error, atrofiar si es inútil
            double growth = alpha_ * std::pow(abs_flux, 1.15);
            double decay = gamma_ * tub.conductivity;
            tub.conductivity += (growth - decay);

            // Digestión de Basura (Garbage Collection): si el túbulo no transporta nada útil, se poda
            if (tub.conductivity < 0.04) {
                tub.is_active = false;
                digested_waste_energy_ += 0.1; // Reciclaje de biomasa
            } else {
                tub.conductivity = std::min(3.5, tub.conductivity);
                tub.age_steps += 1.0;
                active_tubules_count++;
            }
        }
    }

    // Modula los pesos sinápticos y las corrientes motoras basándose en la red de Physarum
    void modulate_synapses_and_motors(std::vector<double>& motor_currents, 
                                      int n_motor = 30) {
        if (motor_currents.size() < static_cast<size_t>(n_motor)) return;

        // Amplificar fuertemente la zona motora conectada por los túbulos más gruesos
        // y suprimir drásticamente las zonas huérfanas/desconectadas
        const auto& win_tgt = targets_[current_winning_target_];
        
        for (size_t t = 0; t < targets_.size(); ++t) {
            bool is_winner = (static_cast<int>(t) == current_winning_target_);
            double factor = is_winner ? (1.5 + win_tgt.chemo_attractant * 2.0) : 0.05;

            for (int k = targets_[t].motor_start_idx; k <= targets_[t].motor_end_idx; ++k) {
                int local_m = k - 214; // Offset de la capa motora
                if (local_m >= 0 && local_m < (int)motor_currents.size()) {
                    motor_currents[local_m] *= factor;
                    if (!is_winner) {
                        motor_currents[local_m] -= 15.0; // Poda e inhibición estricta
                    }
                }
            }
        }
    }

    // Retorna la lista de túbulos gruesos activos para visualización 3D orgánica
    std::vector<PhysarumTubule> get_dominant_tubules(int max_count = 150) const {
        std::vector<PhysarumTubule> dominant;
        for (const auto& tub : tubules_) {
            if (tub.is_active && tub.conductivity > 0.25) {
                dominant.push_back(tub);
            }
        }
        std::sort(dominant.begin(), dominant.end(), [](const PhysarumTubule& a, const PhysarumTubule& b) {
            return a.conductivity > b.conductivity;
        });
        if ((int)dominant.size() > max_count) {
            dominant.resize(max_count);
        }
        return dominant;
    }

    std::string get_winning_target_name() const {
        return (current_winning_target_ >= 0 && current_winning_target_ < (int)targets_.size()) 
            ? targets_[current_winning_target_].name : "VACIO";
    }

    double get_winning_confidence() const {
        return (current_winning_target_ >= 0 && current_winning_target_ < (int)targets_.size())
            ? targets_[current_winning_target_].chemo_attractant : 0.0;
    }

    int get_active_tubule_count() const {
        int c = 0;
        for (const auto& t : tubules_) if (t.is_active) c++;
        return c;
    }

    double get_digested_waste() const { return digested_waste_energy_; }
    double get_presence_accumulator() const { return presence_accumulator_; }
    bool is_presence_active() const { return is_presence_active_; }

private:
    int n_nodes_;
    double gamma_;
    double alpha_;
    double total_biomass_;
    double digested_waste_energy_;
    int current_winning_target_;
    double presence_accumulator_ = 0.0;
    bool is_presence_active_ = false;
    std::deque<double> resp_history_;

    std::vector<PhysarumTubule> tubules_;
    std::vector<std::vector<int>> adjacency_; // Adyacencia: nodo -> índices de túbulos
    std::vector<double> pressure_;
    std::vector<double> sources_;
    std::vector<PhysarumTargetSource> targets_;
};

#endif // PHYSARUM_OPTIMIZER_HPP
