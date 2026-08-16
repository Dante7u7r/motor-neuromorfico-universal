#include "cerebro.hpp"
#include "server.hpp"
#include "synthetic_signal_adapter.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <functional>
#include <vector>
#include <unordered_set>
#include <deque>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Generadores aleatorios estáticos (Semilla fija 1337 para reproducibilidad y estabilidad de save/load)
static std::mt19937 gen(1337);
static std::normal_distribution<double> normal_dist(0.0, 1.0);
static std::uniform_real_distribution<double> rand_dist(0.0, 1.0);

// ============================================================================
// SISTEMA NEUROMODULADOR
// ============================================================================
NeuromodulatorSystem::NeuromodulatorSystem()
    : dopamine(0.5), serotonin(0.5), acetylcholine(0.5), norepinephrine(0.5),
      tau_da(2.0), tau_5ht(8.0), tau_ach(1.0), tau_ne(2.0) {}

void NeuromodulatorSystem::update(double dt_sec, const std::string& brain_state, double prediction_error) {
    double da_target = 0.5 + 0.5 * std::tanh(std::abs(prediction_error) * 2.0);
    if (brain_state == "SLOW_WAVE_SLEEP") {
        da_target = 0.2;
    } else if (brain_state == "REM") {
        da_target = 0.6;
    }

    double ser_target = (brain_state == "AWAKE") ? 0.7 : ((brain_state == "SLOW_WAVE_SLEEP") ? 0.4 : 0.1);
    double ach_target = (brain_state == "AWAKE") ? 0.8 : ((brain_state == "SLOW_WAVE_SLEEP") ? 0.2 : 0.7);
    double ne_target = (brain_state == "AWAKE") ? 0.6 : ((brain_state == "SLOW_WAVE_SLEEP") ? 0.1 : 0.5);

    dopamine += (da_target - dopamine) * dt_sec / tau_da;
    serotonin += (ser_target - serotonin) * dt_sec / tau_5ht;
    acetylcholine += (ach_target - acetylcholine) * dt_sec / tau_ach;
    norepinephrine += (ne_target - norepinephrine) * dt_sec / tau_ne;

    dopamine = std::max(0.0, std::min(dopamine, 1.0));
    serotonin = std::max(0.0, std::min(serotonin, 1.0));
    acetylcholine = std::max(0.0, std::min(acetylcholine, 1.0));
    norepinephrine = std::max(0.0, std::min(norepinephrine, 1.0));
}

// ============================================================================
// REGULADORES HOMEOSTÁTICOS
// ============================================================================
SynapticScaler::SynapticScaler(double target, bool active_val)
    : target_sum_w(target), active(active_val) {}

void SynapticScaler::scale(BrainUnico& brain) {
    if (!active) return;
    
    for (int neuron_id = 0; neuron_id < N_TOTAL; ++neuron_id) {
        double sum_w = 0.0;
        std::vector<int> syn_indices;
        
        for (size_t s_idx = 0; s_idx < brain.synapses.size(); ++s_idx) {
            const auto& s = brain.synapses[s_idx];
            if (s.post == neuron_id && s.is_excitatory > 0.5 && s.is_active > 0.5) {
                sum_w += s.w;
                syn_indices.push_back((int)s_idx);
            }
        }
        
        if (sum_w > target_sum_w && !syn_indices.empty()) {
            double factor = target_sum_w / sum_w;
            double smooth_factor = 1.0 + 0.15 * (factor - 1.0);
            for (int idx : syn_indices) {
                brain.synapses[idx].w *= smooth_factor;
                brain.synapses[idx].w = std::max(0.02, std::min(brain.synapses[idx].w, 2.0));
            }
        }
    }
}

GainController::GainController(double target, double alpha, bool active_val)
    : target_rate(target), alpha_gain(alpha), v_offset(0.0), active(active_val) {}

void GainController::adapt(BrainUnico& brain, double motor_firing) {
    if (!active) return;
    
    double delta_offset = alpha_gain * (motor_firing - target_rate);
    v_offset = std::max(-5.0, std::min(v_offset + delta_offset, 10.0));
}

// ============================================================================
// ESPACIO GLOBAL DE TRABAJO
// ============================================================================
EspacioGlobal::EspacioGlobal(double umbral_ini, int vent, double ganancia)
    : umbral(umbral_ini), ventana(vent), ganancia_broadcast(ganancia),
      steps_sobre_umbral(0), ignicion_activa(false) {}

std::pair<double, bool> EspacioGlobal::tick(BrainUnico& brain, const std::vector<double>& firing_rates) {
    double actividad_ponderada = 0.0;
    double peso_total = 0.0;
    
    // Pesos por región: Sensory=0.2, Hidden=1.5, Thalamus=1.0, PFC=4.0, Motor=3.0, Hippocampus=2.0
    double pesos[6] = {0.2, 1.5, 3.0, 4.0, 1.0, 2.0};

    for (int i = 0; i < N_TOTAL; ++i) {
        int layer = brain.neurons[i].layer_id;
        if (layer >= 0 && layer < 6) {
            actividad_ponderada += pesos[layer] * firing_rates[i];
            peso_total += pesos[layer];
        }
    }

    if (peso_total > 0.0) {
        actividad_ponderada /= peso_total;
    }

    fr_workspace_history.push_back(actividad_ponderada);
    if (fr_workspace_history.size() > (size_t)ventana * 2) {
        fr_workspace_history.pop_front();
    }
    adaptar_umbral();

    bool evento_nuevo = false;
    if (actividad_ponderada > umbral) {
        steps_sobre_umbral++;
        if (steps_sobre_umbral >= 2) {
            ignicion_activa = true;
            evento_nuevo = true;
        }
    } else {
        ignicion_activa = false;
        steps_sobre_umbral = 0;
    }

    return {actividad_ponderada, ignicion_activa};
}

void EspacioGlobal::adaptar_umbral() {
    if (fr_workspace_history.size() < (size_t)ventana) {
        return;
    }
    std::vector<double> recientes(fr_workspace_history.end() - ventana, fr_workspace_history.end());
    std::sort(recientes.begin(), recientes.end());
    int idx = (int)(0.90 * (recientes.size() - 1));
    double nuevo_umbral = recientes[idx];
    umbral = 0.95 * umbral + 0.05 * nuevo_umbral;
}

// ============================================================================
// PCA ONLINE PARA COMPRESIÓN DE FEATURES CSI (Fase 3)
// ============================================================================
OnlinePCA::OnlinePCA(int input_dim_, int n_components_, int max_samples_, double lr)
    : input_dim(input_dim_), n_components(n_components_), max_samples(max_samples_), learning_rate(lr) {
    mean.resize(input_dim, 0.0);
    components.resize(n_components, std::vector<double>(input_dim, 0.0));
    explained_variance.resize(n_components, 0.0);
    singular_values.resize(n_components, 0.0);
    n_samples_seen = 0;
    fitted = false;
}

void OnlinePCA::reset(int new_input_dim, int new_n_components) {
    if (new_input_dim > 0) input_dim = new_input_dim;
    if (new_n_components > 0) n_components = new_n_components;
    mean.assign(input_dim, 0.0);
    components.assign(n_components, std::vector<double>(input_dim, 0.0));
    explained_variance.assign(n_components, 0.0);
    singular_values.assign(n_components, 0.0);
    n_samples_seen = 0;
    fitted = false;
}

void OnlinePCA::partial_fit(const std::vector<double>& x) {
    if ((int)x.size() != input_dim) return;
    
    // Actualizar media incremental
    n_samples_seen++;
    double alpha = 1.0 / n_samples_seen;
    for (int i = 0; i < input_dim; ++i) {
        mean[i] = (1.0 - alpha) * mean[i] + alpha * x[i];
    }
    
    // Centrar datos
    std::vector<double> x_centered(input_dim);
    for (int i = 0; i < input_dim; ++i) {
        x_centered[i] = x[i] - mean[i];
    }
    
    // Regla de Oja incremental para componentes principales
    for (int k = 0; k < n_components; ++k) {
        double y = 0.0;
        for (int i = 0; i < input_dim; ++i) {
            y += components[k][i] * x_centered[i];
        }
        
        // Actualizar componente k
        for (int i = 0; i < input_dim; ++i) {
            components[k][i] += learning_rate * y * (x_centered[i] - y * components[k][i]);
        }
    }
    
    // Gram-Schmidt para ortogonalizar componentes
    for (int k = 0; k < n_components; ++k) {
        for (int j = 0; j < k; ++j) {
            double dot = 0.0;
            for (int i = 0; i < input_dim; ++i) {
                dot += components[k][i] * components[j][i];
            }
            for (int i = 0; i < input_dim; ++i) {
                components[k][i] -= dot * components[j][i];
            }
        }
        // Normalizar
        double norm = 0.0;
        for (int i = 0; i < input_dim; ++i) {
            norm += components[k][i] * components[k][i];
        }
        norm = std::sqrt(norm + 1e-10);
        if (norm > 1e-10) {
            for (int i = 0; i < input_dim; ++i) {
                components[k][i] /= norm;
            }
        }
        // Calcular varianza explicada aproximada
        double var = 0.0;
        for (int i = 0; i < input_dim; ++i) {
            var += components[k][i] * components[k][i];
        }
        explained_variance[k] = var;
        singular_values[k] = std::sqrt(var * n_samples_seen);
    }
    
    if (n_samples_seen > 100) fitted = true;
}

std::vector<double> OnlinePCA::transform(const std::vector<double>& x) const {
    std::vector<double> result(n_components, 0.0);
    if (!fitted) return result;
    
    for (int k = 0; k < n_components; ++k) {
        double y = 0.0;
        for (int i = 0; i < input_dim; ++i) {
            y += components[k][i] * (x[i] - mean[i]);
        }
        result[k] = y;
    }
    return result;
}

std::vector<double> OnlinePCA::inverse_transform(const std::vector<double>& proj) const {
    std::vector<double> result(input_dim, 0.0);
    if (!fitted) return result;
    
    for (int i = 0; i < input_dim; ++i) {
        double val = mean[i];
        for (int k = 0; k < n_components; ++k) {
            val += components[k][i] * proj[k];
        }
        result[i] = val;
    }
    return result;
}

double OnlinePCA::get_explained_variance_ratio(int k) const {
    if (k < 0 || k >= n_components) return 0.0;
    double total = 0.0;
    for (double v : explained_variance) total += v;
    if (total < 1e-10) return 0.0;
    return explained_variance[k] / total;
}

// ============================================================================
// CEREBRO ÚNICO
// ============================================================================
BrainUnico::BrainUnico()
    : time_ms(0.0), step_count(0), brain_state("AWAKE"),
      pruned_synapses(0), created_synapses(0), frustration(0.0), resilience(0.2),
      decay_factor(0.985), current_ring_step(0), spikes_in_current_batch(0),
      sensor_adapter(std::make_unique<SyntheticSignalAdapter>(128, 0.1)) {
    
    // Inicializar búferes de conductancias con delays
    for (int i = 0; i < N_TOTAL; ++i) {
        for (int j = 0; j < 16; ++j) {
            ring_ampa_soma[i][j] = 0.0;
            ring_gaba_soma[i][j] = 0.0;
            for (int b = 0; b < 3; ++b) {
                ring_ampa_dend_branch[i][b][j] = 0.0;
                ring_gaba_dend_branch[i][b][j] = 0.0;
            }
        }
    }

    // Inicializar parámetros de TD-learning y gating
    last_state_value = 0.0;
    alpha_value = 0.02;
    ncc_chaos_state = 0.357;
    cortisol = 0.1;
    melatonina = 0.0;
    for (int i = 0; i < N_SENSORY; ++i) {
        v_if[i] = 0.0;
    }
    
    astrocytes.resize(28);  // 274/10 ≈ 28 grupos
    for (int i = 0; i < 28; ++i) {
        astrocytes[i].group_start = i * 10;
        astrocytes[i].group_end = std::min((i + 1) * 10, N_TOTAL);
        astrocytes[i].calcium = 0.0;
    }

    for (int i = 0; i < N_TOTAL; ++i) {
        w_value[i] = rand_dist(gen) * 0.1;
        pfc_gates[i] = 1.0;
    }

    // 1. Configurar capas de las neuronas
    neurons.resize(N_TOTAL);
    for (int i = 0; i < N_TOTAL; ++i) {
        auto& n = neurons[i];
        if (i < N_SENSORY) {
            n.layer_id = 0; // Sensorial
        } else if (i < N_SENSORY + N_HIDDEN) {
            n.layer_id = 1; // Oculta
        } else if (i < N_SENSORY + N_HIDDEN + N_TALAMUS) {
            n.layer_id = 4; // Tálamo
        } else if (i < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC) {
            n.layer_id = 3; // PFC
        } else if (i < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR) {
            n.layer_id = 2; // Motor
        } else if (i < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR + N_EC) {
            n.layer_id = 6; // Corteza Entorrinal
        } else {
            n.layer_id = 5; // Hipocampo
        }
        n.type = 1; // Excitatoria por defecto
    }

    // 20% de las Ocultas, PFC, Hipocampo son inhibitorias
    for (int i = N_SENSORY + N_HIDDEN - N_HIDDEN/5; i < N_SENSORY + N_HIDDEN; ++i) {
        neurons[i].type = 4;
    }
    for (int i = PFC_GATE_END; i < PFC_WTA_END; ++i) {
        neurons[i].type = 4;
    }
    int hippo_start = N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR + N_EC;
    for (int i = hippo_start + N_HIPO - N_HIPO/5; i < N_TOTAL; ++i) {
        neurons[i].type = 4;
    }

    // 2. Posiciones 3D de visualización y conectividad
    for (int i = 0; i < N_TOTAL; ++i) {
        auto& n = neurons[i];
        n.x = rand_dist(gen) * 80.0 - 40.0;
        n.y = rand_dist(gen) * 80.0 - 40.0;
        if (n.layer_id == 0) n.z = 5.0;
        else if (n.layer_id == 1) n.z = 15.0;
        else if (n.layer_id == 4) n.z = 25.0;
        else if (n.layer_id == 3) n.z = 35.0;
        else if (n.layer_id == 2) n.z = 45.0;
        else if (n.layer_id == 6) n.z = 50.0;
        else if (n.layer_id == 5) n.z = 55.0;
        else n.z = 55.0;

        // Parámetros de membrana LIF
        n.v_rest = -65.0;
        n.v = -65.0 + normal_dist(gen) * 3.0;
        n.v_dend = -65.0 + normal_dist(gen) * 3.0;
        
        n.v_thresh_base = (n.type == 1) ? -55.0 : -57.0;
        n.v_thresh = n.v_thresh_base;
        n.tau_m = (n.type == 1) ? 15.0 : 10.0; // ms
        n.tau_dend = 30.0; // ms
        n.g_coupling = 0.35;
        n.E_ampa = 0.0;
        n.E_gaba = -75.0;

        n.g_ampa_soma = 0.0;
        n.g_gaba_soma = 0.0;
        n.g_ampa_dend = 0.0;
        n.g_gaba_dend = 0.0;
        for (int b = 0; b < 3; ++b) {
            n.g_ampa_dend_branch[b] = 0.0;
            n.g_gaba_dend_branch[b] = 0.0;
        }

        n.delta_v_thresh = (n.type == 1) ? 0.35 : 0.20;
        n.tau_thresh = (n.type == 1) ? 60.0 : 30.0; // ms

        n.noise_base = 1.2;
        n.is_sensory = (n.layer_id == 0) ? 1.0 : 0.0;
        n.cpg_amplitude = (n.layer_id == 0) ? 2.5 : 0.0;
        n.signal_period = SIGNAL_PERIOD; // ms

        n.energy = 1.0;
        n.firing_rate = 0.0;
        n.last_spike_time = -1.0;
        n.frustration = 0.0;
        n.resilience = 0.2;
        n.I_ext = 0.0;
    }

    // 3. Crear sinapsis con conectividad arquitectónicamente válida
    synapses.reserve(N_TOTAL * N_TOTAL * 0.1);
    dist_3d.reserve(N_TOTAL * N_TOTAL * 0.1);
    
    for (int i = 0; i < N_TOTAL; ++i) {
        for (int j = 0; j < N_TOTAL; ++j) {
            if (i == j) continue; // Sin autolazos
            
            int pre_layer = neurons[i].layer_id;
            int post_layer = neurons[j].layer_id;
            
            bool allowed = false;
            double p_connect = 0.0;
            
            // Arquitectura feedforward + recurrencia controlada
            if (pre_layer == 0 && post_layer == 1) { // Sensory → Hidden
                allowed = true; p_connect = 0.30;
            } else if (pre_layer == 0 && post_layer == 6) { // Sensory → EC
                allowed = true; p_connect = 0.30;
            } else if (pre_layer == 1 && post_layer == 4) { // Hidden → Thalamus
                allowed = true; p_connect = 0.40;
            } else if (pre_layer == 4 && post_layer == 3) { // Thalamus → PFC
                allowed = true; p_connect = 0.50;
            } else if (pre_layer == 3 && post_layer == 2) { // PFC → Motor
                allowed = true; p_connect = 0.40;
            } else if (pre_layer == 3 && post_layer == 6) { // PFC → EC
                allowed = true; p_connect = 0.20;
            } else if (pre_layer == 6 && post_layer == 3) { // EC → PFC
                allowed = true; p_connect = 0.20;
            } else if (pre_layer == 6 && post_layer == 5) { // EC → Hippocampus (perforant path)
                allowed = true; p_connect = 0.40;
            } else if (pre_layer == 5 && post_layer == 6) { // Hippocampus → EC (feedback)
                allowed = true; p_connect = 0.30;
            } else if (pre_layer == 5 && post_layer == 3) { // Hippocampus → PFC
                allowed = true; p_connect = 0.20;
            } else if (pre_layer == 1 && post_layer == 1) { // Recurrente Hidden
                allowed = true; p_connect = 0.10;
            } else if (pre_layer == 3 && post_layer == 3) { // Recurrente PFC
                allowed = true; p_connect = 0.15;
            } else if (pre_layer == 2 && post_layer == 2) { // Recurrente Motor
                allowed = true; p_connect = 0.05;
            } else if (pre_layer == 5 && post_layer == 5) { // Recurrente Hippocampus
                allowed = true; p_connect = 0.10;
            } else if (pre_layer == 6 && post_layer == 6) { // Recurrente EC
                allowed = true; p_connect = 0.10;
            } 
            // === TOP-DOWN FEEDBACK DESCENDENTE (Predictive Coding) ===
            else if (pre_layer == 3 && post_layer == 4) { // PFC → Thalamus (Gating descendente)
                allowed = true; p_connect = 0.35;
            } else if (pre_layer == 3 && post_layer == 1) { // PFC → Hidden (Template matching descendente)
                allowed = true; p_connect = 0.25;
            } else if (pre_layer == 4 && post_layer == 0) { // Thalamus → Sensory (Cancelación de eco estático)
                allowed = true; p_connect = 0.20;
            }
            
            if (!allowed) continue;
            if (rand_dist(gen) >= p_connect) continue;
            
            Synapse s;
            s.pre = i;
            s.post = j;
            s.w = 0.0;
            s.myelination = 0.0;
            s.is_excitatory = (neurons[i].type == 1) ? 1.0 : 0.0;
            s.target_is_dendrite = (rand_dist(gen) < 0.5) ? 1.0 : 0.0;
            s.is_active = 1.0;
            s.dendritic_branch = (int)(rand_dist(gen) * 3); // 0, 1, 2

            // Consolidación STC
            s.w_early = 0.0;
            s.w_late = 0.0;
            s.tag = 0.0;

            // Parámetros STP
            s.U_stp = (neurons[i].type == 1) ? 0.12 : 0.5;
            s.tau_d = (neurons[i].type == 1) ? 80.0 : 100.0; // ms
            s.tau_f = (neurons[i].type == 1) ? 100.0 : 150.0; // ms
            s.x_stp = 1.0;
            s.u_stp = s.U_stp;

            s.apre = 0.0;
            s.apre2 = 0.0;
            s.apost = 0.0;
            s.last_update_ms = 0.0;
            s.prune_timer = 0;

            // Retardos basados en distancia 3D
            double dx = neurons[i].x - neurons[j].x;
            double dy = neurons[i].y - neurons[j].y;
            double dz = neurons[i].z - neurons[j].z;
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            
            dist_3d.push_back(dist);
            s.base_delay_ms = dist; // Se calibrará con la máxima distancia más abajo
            synapses.push_back(s);
        }
    }

    // Calibrar delays con respecto a la máxima distancia
    double max_dist = 0.1;
    for (double d : dist_3d) {
        if (d > max_dist) max_dist = d;
    }

    for (size_t k = 0; k < synapses.size(); ++k) {
        double d_norm = dist_3d[k] / max_dist;
        synapses[k].base_delay_ms = 1.5 + 2.5 * d_norm; // Rango [1.5, 4.0] ms
        synapses[k].delay_steps = (int)std::round(synapses[k].base_delay_ms); // Discretizado a ms enteros
        if (synapses[k].delay_steps < 1) synapses[k].delay_steps = 1;
        if (synapses[k].delay_steps > 15) synapses[k].delay_steps = 15;
    }

    // 4. Inicializar pesos para sinapsis activas
    for (size_t k = 0; k < synapses.size(); ++k) {
        auto& s = synapses[k];
        int pre_layer = neurons[s.pre].layer_id;
        int post_layer = neurons[s.post].layer_id;
        
        double scale = 1.0;
        if (pre_layer == 0 && post_layer == 1) scale = 0.8; // Sensory → Hidden
        else if (pre_layer == 0 && post_layer == 6) scale = 0.6; // Sensory → EC
        else if (pre_layer == 1 && post_layer == 4) scale = 1.0; // Hidden → Thalamus
        else if (pre_layer == 4 && post_layer == 3) scale = 1.2; // Thalamus → PFC
        else if (pre_layer == 3 && post_layer == 2) scale = 0.8; // PFC → Motor
        else if (pre_layer == 3 && post_layer == 6) scale = 0.6; // PFC → EC
        else if (pre_layer == 6 && post_layer == 3) scale = 0.8; // EC → PFC
        else if (pre_layer == 6 && post_layer == 5) scale = 1.0; // EC → Hippocampus (perforant)
        
        if (s.is_excitatory > 0.5) {
            s.w = scale * (0.8 + rand_dist(gen) * 0.6);
        } else {
            s.w = scale * 1.2;
        }
        s.myelination = 0.1 + rand_dist(gen) * 0.2;
    }

    
    // Inicializar pares Siamese STDP
    initialize_siamese_pairs();

    // Inicializar listas de sinapsis
    pre_syn_list.assign(N_TOTAL, {});
    post_syn_list.assign(N_TOTAL, {});
    for (size_t k = 0; k < synapses.size(); ++k) {
        pre_syn_list[synapses[k].pre].push_back((int)k);
        post_syn_list[synapses[k].post].push_back((int)k);
    }
    syn_lists_built = true;

    // Inicializar malla protoplásmica de Physarum Polycephalum
    std::vector<double> node_xs(N_TOTAL), node_ys(N_TOTAL), node_zs(N_TOTAL);
    for (int i = 0; i < N_TOTAL; ++i) {
        node_xs[i] = neurons[i].x;
        node_ys[i] = neurons[i].y;
        node_zs[i] = neurons[i].z;
    }
    std::vector<std::pair<int, int>> initial_edges;
    for (const auto& s : synapses) {
        initial_edges.push_back({s.pre, s.post});
    }
    physarum.build_initial_mesh(node_xs, node_ys, node_zs, initial_edges);
}

BrainUnico::BrainUnico(std::unique_ptr<ISensorAdapter> sensor)
    : BrainUnico() {
    sensor_adapter = std::move(sensor);
}

void BrainUnico::initialize_siamese_pairs() {
    siamese_pairs.clear();
    
    // Crear pares contrastivos para cada usuario enrolado
    // Rama A (dendritic_branch=0) = Subject A
    // Rama B (dendritic_branch=1) = Subject B
    // Rama C (dendritic_branch=2) = Crowd/Unknown
    
    // Pares genuinos (mismo sujeto): target_similarity = 1.0
    // Mapeo correcto: USER_A → PFC_VERIFY (188-192) + Motor Z1 (90-94)
    //                 USER_B → PFC_IDENTIFY (198-202) + Motor Z2 (95-99)
    for (int user = 0; user < 2; ++user) {
        int branch = user;
        int base_pfc = (user == 0) ? PFC_VERIFY_START : PFC_IDENTIFY_START;
        int base_motor = (user == 0) ? 90 : 95; // Z1 para USER_A, Z2 para USER_B
        for (int rep = 0; rep < 5; ++rep) {
            SiamesePair pair;
            pair.neuron_a = base_pfc + (rep % 5);
            pair.neuron_b = base_motor + (rep % 5);
            pair.dendritic_branch_a = branch;
            pair.dendritic_branch_b = branch;
            pair.target_similarity = 1.0;
            pair.weight = 0.5;
            pair.margin = siamese_margin;
            pair.active = true;
            siamese_pairs.push_back(pair);
        }
    }
    
    // Pares impostores (sujetos diferentes): target_similarity = 0.0
    for (int user_a = 0; user_a < 2; ++user_a) {
        for (int user_b = 0; user_b < 2; ++user_b) {
            if (user_a == user_b) continue;
            for (int rep = 0; rep < 3; ++rep) {
                SiamesePair pair;
                pair.neuron_a = PFC_IDENTIFY_START + user_a * 5 + (rep % 5);
                pair.neuron_b = PFC_IDENTIFY_START + user_b * 5 + (rep % 5);
                pair.dendritic_branch_a = user_a;
                pair.dendritic_branch_b = user_b;
                pair.target_similarity = 0.0; // Impostor
                pair.weight = 0.5;
                pair.margin = siamese_margin;
                pair.active = true;
                siamese_pairs.push_back(pair);
            }
        }
    }
    
    // Pares crowd vs usuarios
    for (int user = 0; user < 2; ++user) {
        for (int rep = 0; rep < 3; ++rep) {
            SiamesePair pair;
            pair.neuron_a = PFC_IDENTIFY_START + user * 5 + (rep % 5);
            pair.neuron_b = PFC_IDENTIFY_START + 10 + (rep % 4); // PFC-Gate area
            pair.dendritic_branch_a = user;
            pair.dendritic_branch_b = 2; // Crowd branch
            pair.target_similarity = 0.0;
            pair.weight = 0.5;
            pair.margin = siamese_margin;
            pair.active = true;
            siamese_pairs.push_back(pair);
        }
    }
    
    std::cout << "[SIAMESE] Inicializadas " << siamese_pairs.size() 
              << " parejas contrastivas (Genuinas + Impostores)" << std::endl;
}

void BrainUnico::step() {
    time_ms += BATCH_MS;
    step_count++;
    
    frustration = 0.0;
    
    // Ciclo sueño/vigilia biológico: 1800 pasos AWAKE (~90s) → 150 NREM → 50 REM → repite
    int cycle_pos = step_count % 2000;
    if (cycle_pos < 1800) {
        brain_state = "AWAKE";
        melatonina = 0.0;
    } else if (cycle_pos < 1950) {
        brain_state = "SLOW_WAVE_SLEEP";
        melatonina = 0.5 + 0.5 * sin(step_count * 0.1);
    } else {
        brain_state = "REM";
        melatonina = 0.1;
    }

    // Extraer tasas de disparo de los neurons del paso previo para el Workspace
    std::vector<double> current_rates(N_TOTAL);
    for (int i = 0; i < N_TOTAL; ++i) {
        current_rates[i] = neurons[i].firing_rate;
    }

    // 1. Tick del Espacio Global de Consciencia
    auto ws_result = workspace.tick(*this, current_rates);
    double fr_workspace = ws_result.first;
    bool ignicion = ws_result.second;

    // Sincronizar neuromoduladores a cada neurona
    for (int i = 0; i < N_TOTAL; ++i) {
        neurons[i].da = neuromod.dopamine;
        neurons[i].ser = neuromod.serotonin;
        neurons[i].ach = neuromod.acetylcholine;
        neurons[i].frustration = frustration;
        neurons[i].resilience = resilience;
        neurons[i].I_ext = 0.0;
    }

    double target_signal = 0.0;
    double prediction = 0.0;
    double error = 0.0;
    std::vector<double> sensor_currents(N_SENSORY, 0.0);

    if (brain_state == "AWAKE") {
        for (int i = 0; i < N_TOTAL; ++i) {
            if (neurons[i].is_sensory > 0.5) {
                neurons[i].cpg_amplitude = 2.5;
            }
        }

        // Bucle Cerrado: Calcular promedio de energía
        double sum_energy = 0.0;
        for (int i = 0; i < N_TOTAL; ++i) {
            sum_energy += neurons[i].energy;
        }
        double mean_energy = sum_energy / N_TOTAL;

        // Leer corrientes sensoriales del adaptador de sensores
        if (sensor_adapter) {
            sensor_adapter->read_sensory_frame(current_sensory_frame, BATCH_MS * 0.001, time_ms);
            sensor_currents = current_sensory_frame.channels;
        }
        
        // ============================================================================
        // BLOQUE 2: CEREBELO PREDICTIVO (Forward Model & Cancelación de Eco Estático)
        // ============================================================================
        auto filtered_currents = cerebellum.forward_and_adapt(sensor_currents, time_ms);
        sensor_currents = filtered_currents;

        // ============================================================================
        // BLOQUE 1.1 (PLANTAE): RED XILEMA-FLOEMA (Memoria Lenta & Compensación Climática)
        // ============================================================================
        std::vector<float> p_amps(64, 0.0f);
        for (int i = 0; i < 64; ++i) p_amps[i] = static_cast<float>(sensor_currents[i] * 0.25);
        plant_priming.update_climate_adaptation(p_amps, BATCH_MS * 0.001);
        for (int i = 0; i < 64; ++i) {
            sensor_currents[i] = plant_priming.compensate_subcarrier(i, sensor_currents[i]);
        }

        // ============================================================================
        // BLOQUE 1.2 (PLANTAE): TRANSPORTE POLAR DE AUXINAS (Bio-Beamforming)
        // ============================================================================
        double e_rx1 = 0.0, e_rx2 = 0.0;
        for (int i = 0; i < 32; ++i) e_rx1 += sensor_currents[i];
        for (int i = 32; i < 64; ++i) e_rx2 += sensor_currents[i];
        float doppler_centroid = static_cast<float>(sensor_currents[127] * 0.25);
        auxin_beamformer.update_polar_transport(e_rx1, e_rx2, doppler_centroid, BATCH_MS * 0.001);
        double auxin_steered_angle = auxin_beamformer.get_steered_angle_deg();

        // ============================================================================
        // BLOQUE 1.3: CORTEZA PARIETAL & COLÍCULO SUPERIOR (AoA Directo Guiado por Auxinas)
        // ============================================================================
        std::vector<float> p_diff(63, 0.0f);
        for (int i = 0; i < 63; ++i) p_diff[i] = static_cast<float>(sensor_currents[64 + i] * 0.25);
        
        spatial_parietal.process_phase_interferometry(p_diff, p_amps, doppler_centroid, static_cast<float>(auxin_steered_angle), true);

        // Modular canales sensoriales con la sintonía espacial parietal
        const auto& spatial_tuning = spatial_parietal.get_spatial_activity();
        for (size_t i = 0; i < spatial_tuning.size() && i < 16; ++i) {
            sensor_currents[100 + i] += spatial_tuning[i] * 0.15; // Inyectar mapa espacial
        }
        
        // TD-LEARNING (Actor-Critic) & PBWM (Gating de Memoria Prefrontal)
        double reward = sensor_adapter ? sensor_adapter->get_reward() : 0.0;
        
        // 1. Evaluar el valor estimado del estado actual V(t) basándose en actividad de PFC y Hidden
        double current_state_value = 0.0;
        for (int i = N_SENSORY; i < N_SENSORY + N_HIDDEN; ++i) {
            current_state_value += w_value[i] * (neurons[i].firing_rate / 100.0);
        }
        for (int i = PFC_VERIFY_START; i < PFC_WTA_END; ++i) {
            current_state_value += w_value[i] * (neurons[i].firing_rate / 100.0);
        }

        // 2. Recompensa inmediata
        double r_immediate = reward;

        // 3. Error de predicción por diferencia temporal (TD-Error / RPE)
        double gamma = 0.95;
        double delta = r_immediate + gamma * current_state_value - last_state_value;

        // 4. Actualizar los pesos del crítico (regla delta)
        for (int i = N_SENSORY; i < N_SENSORY + N_HIDDEN; ++i) {
            w_value[i] = std::max(0.0, std::min(w_value[i] + alpha_value * delta * (neurons[i].firing_rate / 100.0), 1.5));
        }
        for (int i = PFC_VERIFY_START; i < PFC_WTA_END; ++i) {
            w_value[i] = std::max(0.0, std::min(w_value[i] + alpha_value * delta * (neurons[i].firing_rate / 100.0), 1.5));
        }

        // 5. Guardar valor de estado actual para el siguiente step
        last_state_value = current_state_value;

        // 6. Mapeo de Dopamina Fásica / Tónica basada en RPE
        neuromod.dopamine = 0.5 + 0.5 * std::tanh(delta * 3.0);

        // Si el agente comió (recompensa cruda), recargar energía y limpiar frustración
        if (reward > 0.0) {
            frustration = 0.0;
            for (int i = 0; i < N_TOTAL; ++i) {
                neurons[i].energy = std::min(1.0, neurons[i].energy + 0.30);
            }
        }

        // 7. Lógica de Gating (PBWM) por Ganglios Basales para el PFC
        // El Estriatum recibe input directo del córtex sensorial (biológicamente válido),
        // evitando la asimetría estructural de Hidden-B que colapsaba el detector min(hA, hB).
        // Actividad sensorial directa → proyección tálamo-estriatral
        double sens_A = 0.0, sens_B = 0.0;
        for (int i = 0; i < 5; ++i)  sens_A += neurons[i].firing_rate;
        for (int i = 5; i < 10; ++i) sens_B += neurons[i].firing_rate;
        sens_A /= 5.0;
        sens_B /= 5.0;

            // Integración temporal (filtro de paso bajo) para mitigar fluctuaciones estadísticas
            static double smooth_sens_A = 0.0;
            static double smooth_sens_B = 0.0;
            if (time_ms <= BATCH_MS + 10.0) {
                smooth_sens_A = 0.0;
                smooth_sens_B = 0.0;
            }
            smooth_sens_A += (sens_A - smooth_sens_A) * 0.4;
            smooth_sens_B += (sens_B - smooth_sens_B) * 0.4;

            // Suavizado sensorial a largo plazo para adaptación de umbral (constante ~50s)
            static double smooth_sens_A_lt = 0.0;
            static double smooth_sens_B_lt = 0.0;
            if (time_ms <= BATCH_MS + 10.0) {
                smooth_sens_A_lt = 2.0; // Valor inicial por defecto
                smooth_sens_B_lt = 2.0;
            }
            smooth_sens_A_lt += (sens_A - smooth_sens_A_lt) * 0.01;
            smooth_sens_B_lt += (sens_B - smooth_sens_B_lt) * 0.01;

            // Umbral adaptativo autocalibrable (scale-invariant)
            double sens_thr_A = std::max(0.2, 0.15 * smooth_sens_A_lt);
            double sens_thr_B = std::max(0.2, 0.15 * smooth_sens_B_lt);

            bool A_activo = (smooth_sens_A > sens_thr_A);
            bool B_activo = (smooth_sens_B > sens_thr_B);

            // Compuertas lógicas (AND/XOR/NOR):
            //   Ambos activos   → MULTITUD  (Canal C abierto)
            //   Solo A activo   → SUJETO_A  (Canal A abierto)
            //   Solo B activo   → SUJETO_B  (Canal B abierto)
            //   Ninguno activo  → VACIO     (todos cerrados)
            double target_gate_A = (A_activo && !B_activo) ? 1.0 : 0.05;
            double target_gate_B = (B_activo && !A_activo) ? 1.0 : 0.05;
            double target_gate_C = (A_activo && B_activo)  ? 1.0 : 0.05;

            // ============================================================================
            // BLOQUE 3: GANGLIOS BASALES (Vías Go/No-Go D1/D2 y Decisión Tajante)
            // ============================================================================
            std::vector<double> bg_inputs(4, 0.0);
            bg_inputs[0] = std::max(0.0, 1.0 - (sens_A + sens_B) * 0.5);  // Canal Vacío
            bg_inputs[1] = sens_A * (1.0 + siamese_genuine_similarity);    // Canal Sujeto A
            bg_inputs[2] = sens_B * (1.0 + siamese_impostor_similarity);   // Canal Sujeto B
            bg_inputs[3] = (sens_A * sens_B * 2.0);                         // Canal Multitud
            
            auto bg_decision = basal_ganglia.decide(bg_inputs, neuromod.dopamine);

            // ============================================================================
            // BLOQUE 4: RETROALIMENTACIÓN DESCENDENTE (Top-Down Feedback PFC -> Tálamo)
            // ============================================================================
            // Si el cerebro ya identificó al sujeto con alta confianza, envía feedback
            // inhibitorio al Tálamo para cancelar el eco redundante (Predictive Coding)
            if (bg_decision.confidence > 0.65 && bg_decision.selected_action != 0) {
                // Modulación talámica descendente: enfocar canales relevantes
                for (int i = N_SENSORY + N_HIDDEN; i < N_SENSORY + N_HIDDEN + N_TALAMUS; ++i) {
                    neurons[i].I_ext += (bg_decision.confidence * 4.0); // Resaltar señal relevante
                }
            }

            // Puerta de silencio talámico (simula Núcleo Reticular Talámico - TRN biológico)
            bool is_mapping = sensor_adapter ? sensor_adapter->is_calibrating() : false;
            if (!is_mapping && sens_A < 0.3 && sens_B < 0.3) {
                for (int i = N_SENSORY + N_HIDDEN; i < N_SENSORY + N_HIDDEN + N_TALAMUS; ++i) neurons[i].I_ext -= 6.0;   // Inhibir Relé Talámico
                for (int i = N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR; i < N_TOTAL; ++i) neurons[i].I_ext -= 4.0; // Inhibir Hipocampo
            }

            // ============================================================================
            // BLOQUE 5: MOHO MUCILAGINOSO (Physarum Polycephalum Multi-Perspectiva)
            // ============================================================================
            double doppler_c = sensor_currents[127] * 0.1;
            double doppler_s = 2.0; // Dispersión Doppler estimada
            double csi_ent = 1.5;   // Entropía de canal
            physarum.step_evolution(sensor_currents, doppler_c, doppler_s, csi_ent, siamese_genuine_similarity);

            // Modulación morfogénica de las corrientes motoras
            std::vector<double> motor_ext_currents(N_MOTOR, 0.0);
            for (int m = 0; m < N_MOTOR; ++m) {
                motor_ext_currents[m] = neurons[N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + m].I_ext;
            }
            physarum.modulate_synapses_and_motors(motor_ext_currents, N_MOTOR);
            for (int m = 0; m < N_MOTOR; ++m) {
                neurons[N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + m].I_ext = motor_ext_currents[m];
            }

            // ============================================================================
            // BLOQUE 6: RED MICELIAL FÚNGICA (Fungal Computing Substrate & Anti-Drift)
            // ============================================================================
            double sensory_energy_val = 0.0;
            for (int i = 0; i < 64 && i < N_SENSORY; ++i) sensory_energy_val += sensor_currents[i];
            sensory_energy_val /= 64.0;
            
            bool target_present_val = spatial_parietal.get_last_target().target_present;
            mycelium.step_dynamics(BATCH_MS * 0.001, sensory_energy_val, doppler_c, target_present_val, neurons);

            // Aplicar compensación de deriva térmica ambiental (Anti-Drift RF)
            double drift_comp = mycelium.get_thermal_drift_compensation();
            for (int i = 0; i < N_SENSORY; ++i) {
                sensor_currents[i] = std::max(0.0, sensor_currents[i] - drift_comp * 0.06);
            }

            // ============================================================================
            // BLOQUE 7 (FUNGI): QUORUM SENSING (Conteo Discreto de Aforo y Densidad de Multitud)
            // ============================================================================
            double csi_var_est = spatial_parietal.get_last_target().confidence;
            fungal_quorum.evaluate_quorum(csi_var_est, doppler_s, csi_ent, sensory_energy_val, BATCH_MS * 0.001);

        target_signal = sin(2.0 * M_PI * time_ms / SIGNAL_PERIOD);

        // Guardar en búfer episódico si la novedad/dopamina es alta
        if (neuromod.dopamine > 0.65) {
            EpisodicMemory mem;
            mem.time_ms = time_ms;
            mem.sensory.resize(N_SENSORY);
            for (int i = 0; i < N_SENSORY; ++i) {
                mem.sensory[i] = sensor_currents[i];
            }
            episodic_buffer.push_back(mem);
            if (episodic_buffer.size() > 1000) {
                episodic_buffer.pop_front();
            }
        }

        // Broadcast Consciente global
        if (ignicion) {
            double exceso = std::max(0.0, (fr_workspace - workspace.umbral) / std::max(1.0, workspace.umbral));
            double ganancia = workspace.ganancia_broadcast * (1.0 + exceso);
            
            for (int i = 0; i < N_TOTAL; ++i) {
                if (neurons[i].layer_id >= 1) { // Oculta, Tálamo, PFC, Motor, Hipocampo
                    neurons[i].I_ext += ganancia * 3.5 * (neurons[i].firing_rate / 100.0);
                }
            }
        }
    } else if (brain_state == "SLOW_WAVE_SLEEP") {
        for (int i = 0; i < N_TOTAL; ++i) {
            if (neurons[i].is_sensory > 0.5) {
                neurons[i].cpg_amplitude = 1.0;
            }
        }
        // Replay suave para consolidación lenta (ondas lentas)
        if (step_count % 5 == 0 && !episodic_buffer.empty()) {
            for (int r = 0; r < 5; ++r) {
                int idx = (int)(rand_dist(gen) * episodic_buffer.size());
                const auto& mem = episodic_buffer[idx];
                for (int i = 0; i < N_SENSORY && i < (int)mem.sensory.size(); ++i) {
                    neurons[i].I_ext += mem.sensory[i] * 0.2;
                }
            }
        }
    } else if (brain_state == "REM") {
        for (int i = 0; i < N_TOTAL; ++i) {
            if (neurons[i].is_sensory > 0.5) {
                neurons[i].cpg_amplitude = 0.0;
            }
        }
        sleep_replay();
        // Poda sináptica manejada exclusivamente por structural_plasticity()
    }

    // 2. Correr la simulación numérica por BATCH_MS pasos de 1ms
    spikes_in_current_batch = 0;
    std::vector<int> spike_counts(N_TOTAL, 0);
    std::vector<int> astrocyte_spike_counts(28, 0);

    // Vectores para indexación rápida de sinapsis pre y post (usar miembros de clase)
    if (!syn_lists_built) {
        pre_syn_list.assign(N_TOTAL, {});
        post_syn_list.assign(N_TOTAL, {});
        for (size_t k = 0; k < synapses.size(); ++k) {
            pre_syn_list[synapses[k].pre].push_back((int)k);
            post_syn_list[synapses[k].post].push_back((int)k);
        }
        syn_lists_built = true;
    }

    // Vector para rastrear spikes en el milisegundo actual
    std::vector<bool> current_step_spikes(N_TOTAL, false);

    // STDP siempre activo (aprendizaje hebbiano libre en todo momento)
    bool stdp_active = (brain_state == "AWAKE");

    for (int ms_step = 0; ms_step < (int)BATCH_MS; ++ms_step) {
        double current_time_sec = (time_ms - BATCH_MS + ms_step) / 1000.0;

        // Resetear registro de spikes para este milisegundo discreto
        std::fill(current_step_spikes.begin(), current_step_spikes.end(), false);

        // Actualizar el generador de corriente caótico (Mapa Logístico) para el NCC (Amplitud reducida a 1.5)
        ncc_chaos_state = 3.99 * ncc_chaos_state * (1.0 - ncc_chaos_state);

        // 1. Filtro Sensorial IF (digitalización de entradas RF/CSI en spikes discretos)
        if (brain_state == "AWAKE") {
            for (int i = 0; i < N_SENSORY; ++i) {
                v_if[i] += sensor_currents[i] * 0.001; // dt = 1ms = 0.001s
                if (v_if[i] >= 1.0) {
                    v_if[i] = 0.0;
                    current_step_spikes[i] = true;
                    neurons[i].v = neurons[i].v_rest;
                    neurons[i].last_spike_time = current_time_sec;
                    spike_counts[i]++;
                    spikes_in_current_batch++;
                    int astro_idx2 = i / 10;
                    if (astro_idx2 < 28) astrocyte_spike_counts[astro_idx2]++;
                }
            }

            // Priming neuromodulatorio — sesgo orientado por contexto (Modo Multitud / Entrenamiento)
            // Fuerza moderada-alta: suficiente para grabar trazas STDP sin ser un clamp rígido
            int true_state = -1;
            double trial_timer = 0.0;
            bool is_calib = sensor_adapter ? sensor_adapter->is_calibrating() : false;
            if (is_calib && true_state >= 0 && trial_timer < 10.0) {
                for (int i = N_SENSORY; i < N_TOTAL; ++i) neurons[i].I_ext = 0.0;

                if (true_state == 0) {
                    // VACIO: excitar Z0 activamente, inhibir Z1/Z2/Z3 y callar corteza
                    for (int i = 80; i < 90; ++i) neurons[i].I_ext =  25.0;  // Excitar Z0
                    for (int i = 90; i < 110; ++i) neurons[i].I_ext = -25.0; // Inhibir Z1/Z2/Z3
                    for (int i = N_SENSORY; i < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC; ++i) neurons[i].I_ext =  -8.0;  // Silenciar corteza
                    for (int i = N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR; i < N_TOTAL; ++i) neurons[i].I_ext = -8.0; // Silenciar EC + Hipocampo
                } else if (true_state == 1) {
                    // SUJETO_A: Activar la zona motora Z1 (90-94)
                    for (int i = N_SENSORY; i < N_SENSORY + 10; ++i) neurons[i].I_ext =  10.0;  // Hidden-A stream
                    for (int i = PFC_VERIFY_START; i < PFC_VERIFY_END; ++i) neurons[i].I_ext =  10.0;  // PFC-Verify
                    for (int i = N_SENSORY + 10; i < N_SENSORY + N_HIDDEN; ++i) neurons[i].I_ext = -15.0;  // Inhibir Hidden-B streams
                    for (int i = PFC_IDENTIFY_START; i < PFC_GATE_END; ++i) neurons[i].I_ext = -15.0;  // Inhibir PFC-Identify/Gate
                    
                    for (int i = 90; i < 95; ++i) neurons[i].I_ext =  25.0;  // Motor Z1 (SUJETO_A)
                    for (int i = 80; i < 90; ++i) neurons[i].I_ext = -25.0;  // Inhibir Z0
                    for (int i = 95; i < 106; ++i) neurons[i].I_ext = -25.0;  // Inhibir Z2 y Z3
                } else if (true_state == 2) {
                    // SUJETO_B: Activar la zona motora Z2 (95-99)
                    for (int i = N_SENSORY + 10; i < N_SENSORY + 20; ++i) neurons[i].I_ext =  10.0;  // Hidden-B stream
                    for (int i = PFC_IDENTIFY_START; i < PFC_IDENTIFY_END; ++i) neurons[i].I_ext =  10.0;  // PFC-Identify
                    for (int i = N_SENSORY; i < N_SENSORY + 10; ++i) neurons[i].I_ext = -15.0;  // Inhibir Hidden-A
                    for (int i = PFC_VERIFY_START; i < PFC_VERIFY_END; ++i) neurons[i].I_ext = -15.0;  // Inhibir PFC-Verify
                    for (int i = PFC_GATE_START; i < PFC_GATE_END; ++i) neurons[i].I_ext = -15.0;  // Inhibir PFC-Gate
                    
                    for (int i = 95; i < 100; ++i) neurons[i].I_ext =  25.0;  // Motor Z2 (SUJETO_B)
                    for (int i = 80; i < 95; ++i) neurons[i].I_ext = -25.0;  // Inhibir Z0 y Z1
                    for (int i = 100; i < 106; ++i) neurons[i].I_ext = -25.0; // Inhibir Z3
                } else if (true_state == 3) {
                    // MULTITUD: Activar ambas rutas ocultas, excitar PFC-Gate (80-83) y Motor Z3 (100-105)
                    for (int i = N_SENSORY; i < N_SENSORY + 20; ++i) neurons[i].I_ext =  10.0;  // Hidden-A y Hidden-B
                    for (int i = PFC_GATE_START; i < PFC_GATE_END; ++i) neurons[i].I_ext =  15.0;  // Excitar PFC-Gate
                    for (int i = PFC_VERIFY_START; i < PFC_IDENTIFY_END; ++i) neurons[i].I_ext = -25.0;  // Inhibir PFC-Verify e Identify
                    
                    for (int i = 100; i < 106; ++i) neurons[i].I_ext =  25.0; // Motor Z3 (MULTITUD)
                    for (int i = 80; i < 100; ++i) neurons[i].I_ext = -25.0;  // Inhibir Z0, Z1 y Z2
                }
            } else if (true_state >= 0 && trial_timer < 10.0) {
                // Inferencia autónoma: bias suave según estado CSI
                for (int i = N_SENSORY; i < N_TOTAL; ++i) neurons[i].I_ext = 0.0;
                for (int i = 80; i < 110; ++i) neurons[i].I_ext = -3.0;
                if (true_state == 0) {
                    for (int i = 80; i < 86; ++i) neurons[i].I_ext = 5.0;
                } else if (true_state == 1) {
                    for (int i = 90; i < 95; ++i) neurons[i].I_ext = 5.0;
                    for (int i = PFC_VERIFY_START; i < PFC_VERIFY_END; ++i) neurons[i].I_ext = 5.0;
                } else if (true_state == 2) {
                    for (int i = 95; i < 100; ++i) neurons[i].I_ext = 5.0;
                    for (int i = PFC_IDENTIFY_START; i < PFC_IDENTIFY_END; ++i) neurons[i].I_ext = 5.0;
                } else if (true_state == 3) {
                    for (int i = 100; i < 106; ++i) neurons[i].I_ext = 5.0;
                    for (int i = PFC_GATE_START; i < PFC_GATE_END; ++i) neurons[i].I_ext = 3.0;
                }
            } else {
                for (int i = N_SENSORY; i < N_TOTAL; ++i) neurons[i].I_ext = 0.0;
            }
        }

        // Inyectar conductancias y determinar qué neuronas necesitan dinámica completa
        // (event-driven: solo ~10-20% activas por ms en simulación típica)
        bool active_ms[N_TOTAL];
        for (int i = 0; i < N_TOTAL; ++i) {
            auto& n = neurons[i];

            // Inyectar desde buffers circulares
            n.g_ampa_soma += ring_ampa_soma[i][current_ring_step];
            n.g_gaba_soma += ring_gaba_soma[i][current_ring_step];
            ring_ampa_soma[i][current_ring_step] = 0.0;
            ring_gaba_soma[i][current_ring_step] = 0.0;

            for (int b = 0; b < 3; ++b) {
                n.g_ampa_dend_branch[b] += ring_ampa_dend_branch[i][b][current_ring_step];
                n.g_gaba_dend_branch[b] += ring_gaba_dend_branch[i][b][current_ring_step];
                ring_ampa_dend_branch[i][b][current_ring_step] = 0.0;
                ring_gaba_dend_branch[i][b][current_ring_step] = 0.0;
            }

            // Criterios de actividad para dinámica completa
            bool has_input = (n.g_ampa_soma + n.g_gaba_soma +
                              n.g_ampa_dend_branch[0] + n.g_gaba_dend_branch[0] +
                              n.g_ampa_dend_branch[1] + n.g_gaba_dend_branch[1] +
                              n.g_ampa_dend_branch[2] + n.g_gaba_dend_branch[2]) > 0.001;
            bool has_ext = (n.I_ext != 0.0);
            bool near_thresh = (n.v > n.v_thresh - 10.0);
            bool is_sensory_active = (n.is_sensory > 0.5 && brain_state != "REM");
            bool is_chaos_ncc = (i >= 90 && i < 92);

            active_ms[i] = has_input || has_ext || near_thresh || is_sensory_active || is_chaos_ncc;
        }

        // Evaluar dinámica: completa para activas, decaimiento pasivo para inactivas
        for (int i = 0; i < N_TOTAL; ++i) {
            auto& n = neurons[i];

            if (active_ms[i]) {
                // === DINÁMICA COMPLETA (activa) ===
                bool is_clamped = (n.I_ext < -5.0);

                double I_cpg = n.cpg_amplitude * sin(2.0 * M_PI * current_time_sec / (n.signal_period / 1000.0)) * n.is_sensory;

                double I_syn_soma = n.g_ampa_soma * (n.E_ampa - n.v) + n.g_gaba_soma * (n.E_gaba - n.v);
                double I_syn_dend = 0.0;
                for (int b = 0; b < 3; ++b) {
                    I_syn_dend += n.g_ampa_dend_branch[b] * (n.E_ampa - n.v_dend) + n.g_gaba_dend_branch[b] * (n.E_gaba - n.v_dend);
                }
                double I_coupling = n.g_coupling * (n.v_dend - n.v);

                int astro_idx = i / 10;
                if (astro_idx >= 28) astro_idx = 27;
                double astro_offset = (astrocytes[astro_idx].calcium > 0.35) ? 2.5 : 0.0;
                double gain_offset = (n.layer_id == 2) ? gain_control.v_offset : 0.0;
                double v_thresh_base_effective = n.v_thresh_base + 2.0 * std::pow(1.0 - n.energy, 2) + astro_offset + gain_offset;
                n.v_thresh += (v_thresh_base_effective - n.v_thresh) * 0.001 / (n.tau_thresh / 1000.0);

                double frustration_factor = 1.0 + 1.2 * n.frustration;
                double cortisol_factor = 1.0 + 1.0 * cortisol;
                double ach_factor = 0.8 + 0.4 * n.ach;
                double energy_factor = 0.2 + 0.8 * n.energy;
                double xi = normal_dist(gen);
                double noise_term = n.noise_base * frustration_factor * cortisol_factor * ach_factor * energy_factor * std::sqrt(1.0 / (n.tau_m / 1000.0)) * xi * std::sqrt(0.001);

                double I_chaos = (i >= 90 && i < 92) ? (ncc_chaos_state * 1.5) : 0.0;
                if (is_clamped) {
                    n.v = n.v_rest;
                } else if (current_time_sec - n.last_spike_time < 0.002) {
                    n.v = n.v_rest;
                } else {
                    double tau_m_sec = n.tau_m / 1000.0;
                    double sum_g = n.g_ampa_soma + n.g_gaba_soma;
                    double tau_eff = tau_m_sec / (1.0 + sum_g);
                    double ne_gain = 1.0 + 0.5 * neuromod.norepinephrine;
                    double v_inf = (n.v_rest + n.g_ampa_soma * n.E_ampa + n.g_gaba_soma * n.E_gaba + I_coupling + n.I_ext * ne_gain + I_cpg + I_chaos) / (1.0 + sum_g);
                    double factor = std::exp(-0.001 / tau_eff);
                    n.v = n.v * factor + v_inf * (1.0 - factor) + noise_term;
                }

                double tau_dend_sec = n.tau_dend / 1000.0;
                double sum_g_dend = 0.0;
                double sum_g_dend_weighted = 0.0;
                for (int b = 0; b < 3; ++b) {
                    sum_g_dend += n.g_ampa_dend_branch[b] + n.g_gaba_dend_branch[b];
                    sum_g_dend_weighted += n.g_ampa_dend_branch[b] * n.E_ampa + n.g_gaba_dend_branch[b] * n.E_gaba;
                }
                double tau_eff_dend = tau_dend_sec / (1.0 + sum_g_dend);
                double v_inf_dend = (n.v_rest + sum_g_dend_weighted) / (1.0 + sum_g_dend);
                double factor_dend = std::exp(-0.001 / tau_eff_dend);
                n.v_dend = n.v_dend * factor_dend + v_inf_dend * (1.0 - factor_dend);

                // Decaimiento pasivo de conductancias
                n.g_ampa_soma *= 0.8;
                n.g_gaba_soma *= 0.9;
                for (int b = 0; b < 3; ++b) {
                    n.g_ampa_dend_branch[b] *= 0.8;
                    n.g_gaba_dend_branch[b] *= 0.9;
                }

                // Verificar Spikes
                if (!is_clamped && n.v > n.v_thresh) {
                    current_step_spikes[i] = true;
                    n.v = n.v_rest;
                    n.last_spike_time = current_time_sec;
                    n.v_thresh = std::max(-60.0, std::min(n.v_thresh + n.delta_v_thresh, -35.0));

                    spike_counts[i]++;
                    spikes_in_current_batch++;
                    int astro_idx2 = i / 10;
                    if (astro_idx2 < 28) astrocyte_spike_counts[astro_idx2]++;
                }
            } else {
                // === DECAIMIENTO PASIVO RÁPIDO (inactiva) ===
                n.v += (n.v_rest - n.v) * 0.001 / (n.tau_m / 1000.0);
                n.v_dend += (n.v_rest - n.v_dend) * 0.001 / (n.tau_dend / 1000.0);
                n.v_thresh += (n.v_thresh_base - n.v_thresh) * 0.001 / (n.tau_thresh / 1000.0);
                n.g_ampa_soma *= 0.8;
                n.g_gaba_soma *= 0.9;
                for (int b = 0; b < 3; ++b) {
                    n.g_ampa_dend_branch[b] *= 0.8;
                    n.g_gaba_dend_branch[b] *= 0.9;
                }
            }
        }

        // Procesar plasticidad y propagación para los spikes ocurridos en este milisegundo
        double t_ms = time_ms - BATCH_MS + ms_step;

        for (int i = 0; i < N_TOTAL; ++i) {
            if (!current_step_spikes[i]) continue;

            // Pre-spike en neurona i
            for (int k : pre_syn_list[i]) {
                auto& s = synapses[k];
                
                // Decaimiento STDP y recuperación STP por evento (Event-driven)
                double dt = t_ms - s.last_update_ms;
                if (dt > 0.0) {
                    if (dt > 100.0) {
                        s.apre = 0.0;
                        s.apre2 = 0.0;
                        s.apost = 0.0;
                    } else {
                        double factor = std::exp(-dt / 20.0);
                        s.apre *= factor;
                        s.apre2 *= std::exp(-dt / 100.0);
                        s.apost *= factor;
                    }

                    // Recuperación pasiva de recursos y facilitación de la sinapsis (Tsodyks-Markram)
                    s.x_stp = 1.0 - (1.0 - s.x_stp) * std::exp(-dt / s.tau_d);
                    s.u_stp = s.U_stp + (s.u_stp - s.U_stp) * std::exp(-dt / s.tau_f);

                    s.last_update_ms = t_ms;
                }

                // Dinámica de activación de STP por el spike actual
                s.u_stp = std::max(0.0, std::min(s.u_stp + s.U_stp * (1.0 - s.u_stp), 1.0));
                double release = s.u_stp * s.x_stp;
                s.x_stp = std::max(0.0, std::min(s.x_stp - release, 1.0));

                double weight_factor = 1.0;
                if (s.post >= PFC_VERIFY_START && s.post < PFC_WTA_END) {
                    weight_factor = pfc_gates[s.post]; // Gating por Ganglios Basales
                }

                // Inyección Híbrida del NCC a la corteza
                double chaos_modulation = 1.0;
                if (s.pre >= 90 && s.pre < 92 && s.post < PFC_VERIFY_START) {
                    chaos_modulation = 0.05 + 0.35 * frustration;
                }

                double effective_weight = s.w * release * (1.0 + 0.6 * s.myelination) * weight_factor * chaos_modulation * 2.5 * s.is_active;

                // Agendar en buffer circular correspondiente (rama dendrítica específica)
                int slot = (current_ring_step + s.delay_steps) % 16;
                int branch = s.dendritic_branch;
                if (branch < 0) branch = 0;
                if (branch > 2) branch = 2;
                
                if (s.is_excitatory > 0.5) {
                    if (s.target_is_dendrite > 0.5) {
                        ring_ampa_dend_branch[s.post][branch][slot] += effective_weight;
                    } else {
                        ring_ampa_soma[s.post][slot] += effective_weight;
                    }
                } else {
                    if (s.target_is_dendrite > 0.5) {
                        ring_gaba_dend_branch[s.post][branch][slot] += effective_weight;
                    } else {
                        ring_gaba_soma[s.post][slot] += effective_weight;
                    }
                }

                // Apre + traza lenta (triplete STDP)
                s.apre += 0.015 * s.is_excitatory;
                s.apre2 += 0.015 * s.is_excitatory;
                double da_post = neurons[s.post].da;
                double frustration_post = neurons[s.post].frustration;
                double ser_post = neurons[s.post].ser;
                
                // Modulación LTD por neuromodulación (post-pre, mediada por apost)
                double scale_factor = (((1.0 - da_post) / 0.5) + frustration_post) / (0.5 + 1.5 * ser_post);
                if (stdp_active) {
                    s.w = std::max(0.0, std::min(s.w + s.apost * s.is_excitatory * s.is_active * scale_factor, 2.0));
                }
            }

            // Post-spike en neurona i
            for (int k : post_syn_list[i]) {
                auto& s = synapses[k];
                
                // Decaimiento STDP por evento (Event-driven)
                double dt = t_ms - s.last_update_ms;
                if (dt > 0.0) {
                    if (dt > 100.0) {
                        s.apre = 0.0;
                        s.apre2 = 0.0;
                        s.apost = 0.0;
                    } else {
                        double factor = std::exp(-dt / 20.0);
                        s.apre *= factor;
                        s.apre2 *= std::exp(-dt / 100.0);
                        s.apost *= factor;
                    }
                    s.last_update_ms = t_ms;
                }

                // Apost + LTD (STDP)
                s.apost += -0.01575 * s.is_excitatory;
                double da_post = neurons[s.post].da;
                double ach_post = neurons[s.post].ach;
                double ser_post = neurons[s.post].ser;

                // Modulación LTP triplete (dependiente de frecuencia: apre × apre2)
                double scale_factor = (da_post / 0.5) * (1.0 + 2.5 * ach_post) / (0.5 + 1.5 * ser_post);
                if (stdp_active) {
                    s.w = std::max(0.0, std::min(s.w + s.apre * s.apre2 * s.is_excitatory * s.is_active * scale_factor * 15.0, 2.0));
                }
            }
        }

        // 2. Capa Glial: Astrocitos locales para prevenir la epilepsia (cada 20 ms)
        if (ms_step % 20 == 0) {
            for (int a = 0; a < 28; ++a) {
                double spikes_norm = (double)astrocyte_spike_counts[a];
                astrocytes[a].calcium = 0.95 * astrocytes[a].calcium + 0.05 * spikes_norm;
                astrocyte_spike_counts[a] = 0; // resetear contador
            }
        }

        current_ring_step = (current_ring_step + 1) % 16;
    }

    // 3. Actualizar tasas de disparo
    for (int i = 0; i < N_TOTAL; ++i) {
        neurons[i].firing_rate = spike_counts[i] / (BATCH_MS * 0.001); // Convertir a Hz
    }

    // Enviar comandos motores al adaptador / hardware (solo si el cerebro está despierto)
    if (brain_state == "AWAKE") {
        std::vector<double> motor_firing_rates(N_MOTOR);
        for (int i = 0; i < N_MOTOR; ++i) {
            motor_firing_rates[i] = neurons[N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + i].firing_rate;
        }
        if (sensor_adapter) {
            sensor_adapter->send_motor_feedback(motor_firing_rates, time_ms);
        }
    }

    // Actualizar prune_timer para poda: incrementa si sinapsis débil, resetea si fuerte
    for (size_t k = 0; k < synapses.size(); ++k) {
        if (synapses[k].w < 0.05) {
            synapses[k].prune_timer++;
        } else {
            synapses[k].prune_timer = 0;
        }
    }

    // Calcular disparo motor promedio para la homeostasis
    double motor_firing_sum = 0.0;
    int motor_count = 0;
    for (int i = N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC; i < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR; ++i) {
        motor_firing_sum += neurons[i].firing_rate;
        motor_count++;
    }
    double motor_firing = (motor_count > 0) ? (motor_firing_sum / motor_count) : 0.0;
    
    // Adaptación del umbral de ganancia
    gain_control.adapt(*this, motor_firing);

    prediction = motor_firing / 15.0;
    error = 0.0;

    // Actualizar frustración y resiliencia
    if (brain_state == "AWAKE") {
        if (std::abs(error) > 0.35) {
            frustration = std::min(1.0, frustration + 0.06);
        } else {
            frustration = std::max(0.0, frustration - 0.04);
            resilience = std::min(1.0, resilience + 0.01);
        }
    } else {
        frustration = std::max(0.0, frustration - 0.02);
    }

    // Actualizar sistema endocrino neuromodulador y hormonas
    neuromod.update(BATCH_MS * 0.001, brain_state, error);
    cortisol = 0.95 * cortisol + 0.05 * frustration;

    // Consumo de energía celular metabólica
    // Regulado por astrocitos: liberan energía (lactato) según su calcio
    for (int i = 0; i < N_TOTAL; ++i) {
        int a_idx = i / 10;
        if (a_idx >= 28) a_idx = 27;
        double astro_energy_boost = (astrocytes[a_idx].calcium > 0.3) ? 0.02 : 0.0;

        if (spike_counts[i] > 0) {
            neurons[i].energy = std::max(0.0, neurons[i].energy - 0.02 + astro_energy_boost);
        } else {
            neurons[i].energy = std::min(1.0, neurons[i].energy + 0.03 + astro_energy_boost);
        }
    }

    // Reguladores homeostáticos
    if (step_count % 10 == 0) {
        homeostasis();
    }
    if (step_count % 20 == 0) {
        structural_plasticity();
    }

    // Mielinización dinámica
    if (neuromod.dopamine > 0.6) {
        for (size_t k = 0; k < synapses.size(); ++k) {
            auto& s = synapses[k];
            if (s.w > 0.1 && s.is_excitatory > 0.5) {
                s.myelination = std::min(1.0, s.myelination + 0.03);
                // recalcular delay basándose en la velocidad de la mielina
                s.delay_steps = (int)std::round(s.base_delay_ms / (1.0 + 3.0 * s.myelination));
                if (s.delay_steps < 1) s.delay_steps = 1;
                if (s.delay_steps > 15) s.delay_steps = 15;
            }
        }
    }

    // Siamese STDP update (contrastive learning para biometría)
    if (stdp_active) {
        update_siamese_stdp(current_step_spikes, spike_counts);
    }

    // Registrar historial
    double w_sum = 0.0;
    double w_max = 0.0;
    int w_active_count = 0;
    for (const auto& s : synapses) {
        if (s.is_active > 0.5) {
            w_sum += s.w;
            if (s.w > w_max) w_max = s.w;
            w_active_count++;
        }
    }
    double w_mean = (w_active_count > 0) ? (w_sum / w_active_count) : 0.0;

    double energy_sum = 0.0;
    for (const auto& n : neurons) {
        energy_sum += n.energy;
    }
    double energy_mean = energy_sum / N_TOTAL;

    HistoryEntry entry;
    entry.time = time_ms;
    entry.step = step_count;
    if (brain_state == "AWAKE") entry.state_code = 0;
    else if (brain_state == "SLOW_WAVE_SLEEP") entry.state_code = 1;
    else entry.state_code = 2;
    entry.da = neuromod.dopamine;
    entry.ser = neuromod.serotonin;
    entry.ach = neuromod.acetylcholine;
    entry.w_mean = w_mean;
    entry.w_max = w_max;
    entry.synapses = w_active_count;
    entry.spikes = spikes_in_current_batch;
    entry.energy_mean = energy_mean;
    entry.pruned = pruned_synapses;
    entry.created = created_synapses;
    entry.prediction = prediction;
    entry.target = target_signal;
    entry.workspace_fr = fr_workspace;
    entry.workspace_umbral = workspace.umbral;
    entry.ignicion = ignicion;

    history.push_back(entry);
    if (history.size() > 1000) {
        history.erase(history.begin());
    }

    // Actualizar dinámica fisiológica y biomédica
    update_vital_signs(BATCH_MS * 0.001);

    // Construir JSON y empujar al Servidor Web
    update_json_data(get_state_json());
}

void BrainUnico::update_vital_signs(double dt_sec) {
    const auto& sp_target = spatial_parietal.get_last_target();
    double dop_c = (N_TOTAL > 127) ? (neurons[127].I_ext * 0.1) : 0.0;
    std::string winning_class = physarum.get_winning_target_name();

    if (!sp_target.target_present && spikes_in_current_batch < 4000) {
        vital_signs.bpm = 0.0;
        vital_signs.rpm = 0.0;
        vital_signs.activity_label = "SALA VACIA / REPOSO TERMICO";
        vital_signs.activity_intensity = 0.0;

        double r_ecg = ((rand() % 100) / 100.0 - 0.5) * 0.04;
        double r_resp = ((rand() % 100) / 100.0 - 0.5) * 0.04;
        vital_signs.waveform_ecg.push_back(r_ecg);
        if (vital_signs.waveform_ecg.size() > 60) vital_signs.waveform_ecg.erase(vital_signs.waveform_ecg.begin());
        vital_signs.waveform_resp.push_back(r_resp);
        if (vital_signs.waveform_resp.size() > 60) vital_signs.waveform_resp.erase(vital_signs.waveform_resp.begin());
        return;
    }

    // Clasificación cinemática refinada por RCS y Espectro
    if (winning_class == "VACIO" || (!sp_target.target_present && spikes_in_current_batch < 6000)) {
        vital_signs.activity_label = "SALA VACIA / REPOSO TERMICO";
        vital_signs.activity_intensity = 0.0;
    } else if (winning_class == "MASCOTA") {
        vital_signs.activity_label = "MASCOTA EN MOVIMIENTO / CADENCIA 4 PATAS";
        vital_signs.activity_intensity = 0.65;
    } else if (std::abs(dop_c) > 2.0) {
        vital_signs.activity_label = "CAMINANDO / MOVIMIENTO DINAMICO";
        vital_signs.activity_intensity = 0.95;
    } else if (std::abs(dop_c) > 0.20 || spikes_in_current_batch > 22000) {
        vital_signs.activity_label = "MICRO-MOVIMIENTO (TECLEO / GESTOS)";
        vital_signs.activity_intensity = 0.55;
    } else {
        vital_signs.activity_label = "REPOSO / RESPIRACION PASIVA";
        vital_signs.activity_intensity = 0.15;
    }

    // Frecuencias biomédicas diferenciadas por clase biológica
    if (winning_class == "MASCOTA") {
        vital_signs.bpm = 115.0 + 15.0 * std::sin(time_ms * 0.0003);
        vital_signs.rpm = 28.0 + 4.0 * std::cos(time_ms * 0.0002);
    } else if (winning_class == "HUMANO_B") {
        vital_signs.bpm = 78.0 + 6.0 * std::sin(time_ms * 0.0002);
        vital_signs.rpm = 17.0 + 2.0 * std::cos(time_ms * 0.00015);
    } else {
        vital_signs.bpm = 70.0 + 5.0 * std::sin(time_ms * 0.00025) + (neuromod.dopamine - 0.5) * 12.0;
        vital_signs.rpm = 15.0 + 2.0 * std::cos(time_ms * 0.0002);
    }

    // Fases oscilatorias
    vital_signs.ecg_phase += (vital_signs.bpm / 60.0) * 2.0 * 3.1415926535 * dt_sec;
    if (vital_signs.ecg_phase > 2.0 * 3.1415926535) vital_signs.ecg_phase -= 2.0 * 3.1415926535;

    vital_signs.resp_phase += (vital_signs.rpm / 60.0) * 2.0 * 3.1415926535 * dt_sec;
    if (vital_signs.resp_phase > 2.0 * 3.1415926535) vital_signs.resp_phase -= 2.0 * 3.1415926535;

    // Síntesis morfológica de la onda de pulso
    double p_wave = 0.15 * std::exp(-std::pow((vital_signs.ecg_phase - 1.2) / 0.25, 2.0));
    double q_wave = -0.15 * std::exp(-std::pow((vital_signs.ecg_phase - 2.8) / 0.1, 2.0));
    double r_wave = 1.0 * std::exp(-std::pow((vital_signs.ecg_phase - 3.14159) / 0.12, 2.0));
    double s_wave = -0.25 * std::exp(-std::pow((vital_signs.ecg_phase - 3.4) / 0.12, 2.0));
    double t_wave = 0.3 * std::exp(-std::pow((vital_signs.ecg_phase - 4.5) / 0.45, 2.0));
    double ecg_val = p_wave + q_wave + r_wave + s_wave + t_wave + ((rand() % 100) / 100.0 - 0.5) * 0.03;

    double resp_val = std::sin(vital_signs.resp_phase) + ((rand() % 100) / 100.0 - 0.5) * 0.02;

    vital_signs.waveform_ecg.push_back(ecg_val);
    if (vital_signs.waveform_ecg.size() > 60) vital_signs.waveform_ecg.erase(vital_signs.waveform_ecg.begin());

    vital_signs.waveform_resp.push_back(resp_val);
    if (vital_signs.waveform_resp.size() > 60) vital_signs.waveform_resp.erase(vital_signs.waveform_resp.begin());
}

void BrainUnico::update_siamese_stdp(const std::vector<bool>& spikes, const std::vector<int>& spike_counts) {
    siamese_genuine_similarity = 0.0;
    siamese_impostor_similarity = 0.0;
    int genuine_count = 0, impostor_count = 0;
    
    for (auto& pair : siamese_pairs) {
        if (!pair.active) continue;
        
        // Actualizar trazas STDP usando spike_counts (no el bool)
        pair.trace_a *= std::exp(-BATCH_MS / siamese_trace_decay);
        pair.trace_b *= std::exp(-BATCH_MS / siamese_trace_decay);
        
        int count_a = (pair.neuron_a < N_TOTAL) ? spike_counts[pair.neuron_a] : 0;
        int count_b = (pair.neuron_b < N_TOTAL) ? spike_counts[pair.neuron_b] : 0;
        pair.trace_a += count_a;
        pair.trace_b += count_b;
        
        // Similaridad normalizada con tanh
        double similarity = pair.trace_a * pair.trace_b;
        double sim_norm = std::tanh(similarity * 1e-5);
        double error = pair.target_similarity - sim_norm;
        pair.weight += siamese_lr * error;
        pair.weight = std::max(0.0, std::min(pair.weight, 2.0));
        
        // Modificar sinapsis real PFC→Motor
        if (pair.neuron_a < N_TOTAL && pair.neuron_b < N_TOTAL) {
            for (int k : pre_syn_list[pair.neuron_a]) {
                auto& s = synapses[k];
                if (s.post == pair.neuron_b && s.is_excitatory > 0.5) {
                    s.w += siamese_lr * error * 0.1;
                    s.w = std::max(0.02, std::min(s.w, 2.0));
                    break;
                }
            }
        }
        
        // Acumular métricas (usando sim_norm para rango [0, 1])
        if (pair.target_similarity > 0.5) {
            siamese_genuine_similarity += sim_norm;
            genuine_count++;
        } else {
            siamese_impostor_similarity += sim_norm;
            impostor_count++;
        }
    }
    
    if (genuine_count > 0) siamese_genuine_similarity /= genuine_count;
    if (impostor_count > 0) siamese_impostor_similarity /= impostor_count;
}

void BrainUnico::homeostasis() {
    // Escalado homeostático lento de pesos
    for (size_t k = 0; k < synapses.size(); ++k) {
        auto& s = synapses[k];
        
        // Forzar peso a 0.0 si la sinapsis está inactiva (pruning real)
        if (s.is_active < 0.5) {
            s.w = 0.0;
            continue;
        }

        // Evitar el escalado multiplicativo basado en la tasa de disparo para la capa motora (output de clasificación)
        if (s.post >= N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC && 
            s.post < N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR) {
            s.w *= decay_factor;
            s.w = std::max(0.0, std::min(s.w, 2.0));
            continue;
        }

        double post_fr = neurons[s.post].firing_rate;
        
        // Multiplicativo: deprimir si está muy hiperactiva, potenciar si está hipoactiva
        if (post_fr > 8.0) {
            s.w *= (s.is_excitatory > 0.5) ? 0.98 : 1.02; // Hiperactiva (excitatorias bajan, inhibitorias suben)
        } else if (post_fr < 1.0) {
            s.w *= (s.is_excitatory > 0.5) ? 1.01 : 0.99; // Hipoactiva (excitatorias suben, inhibitorias bajan)
        }
        
        // Aplicar decaimiento por desuso pasivo
        s.w *= decay_factor;
        s.w = std::max(0.02, std::min(s.w, 2.0));
    }

    // Escalado astrocítico
    scaler.scale(*this);

    // Normalización biológica suave (Homeostatic Synaptic Scaling asintótico):
    if (brain_state == "SLOW_WAVE_SLEEP") {
        double sum = 0.0; int cnt = 0;
        for (const auto& s : synapses) {
            if (s.is_active > 0.5) { sum += s.w; cnt++; }
        }
        if (cnt > 0) {
            double mean = sum / cnt;
            double target_mean = 1.60;
            if (std::abs(mean - target_mean) > 0.04) {
                double factor = target_mean / mean;
                // Relajación suave (solo 3% por paso): elimina el efecto diente de sierra
                double smooth_factor = 1.0 + 0.03 * (factor - 1.0);
                for (auto& s : synapses) {
                    if (s.is_active > 0.5)
                        s.w = std::max(0.02, std::min(s.w * smooth_factor, 2.0));
                }
            }
        }
    }
}

void BrainUnico::structural_plasticity() {
    // 1. Poda Neurobiológica por Silenciamiento (Sinapsis Silentes):
    // En lugar de borrar elementos de memoria RAM, las sinapsis deprimidas pasan a estado silente (dormidas).
    // Esto elimina las realocaciones continuas de vectores y preserva la arquitectura latente.
    for (size_t k = 0; k < synapses.size(); ++k) {
        auto& s = synapses[k];
        if (s.is_excitatory > 0.5 && s.w < 0.01 && s.prune_timer > 100) {
            if (s.is_active > 0.5) {
                s.is_active = 0.0;
                s.w = 0.0;
                s.prune_timer = 0;
                pruned_synapses++;
            }
        }
    }

    if (synapses.size() >= 40000) return; // Límite de RAM

    // 2. Construir conjunto de conexiones existentes para búsqueda rápida
    //    Solo entre neuronas activas (restringe búsqueda)
    std::unordered_set<int> active_neurons;
    for (int i = 0; i < N_TOTAL; ++i) {
        if (neurons[i].firing_rate > 3.0) {
            active_neurons.insert(i);
        }
    }
    if (active_neurons.size() < 4) return; // Poca actividad, no crear

    std::unordered_set<int> existing_conn;
    for (const auto& s : synapses) {
        if (s.is_active > 0.5) {
            existing_conn.insert((s.pre << 16) | s.post);
        }
    }

    // 3. Sinaptogénesis y Des-silenciamiento de Sinapsis Silentes
    std::vector<int> active_list(active_neurons.begin(), active_neurons.end());
    int attempts = 0;
    int created_here = 0;
    while (attempts < 200 && created_here < 20 && synapses.size() < 40000) {
        attempts++;
        int pre = active_list[(int)(rand_dist(gen) * active_list.size())];
        int post = active_list[(int)(rand_dist(gen) * active_list.size())];
        if (pre == post) continue;

        // Des-silenciar si ya existía una sinapsis dormida
        bool found_silent = false;
        for (auto& s : synapses) {
            if (s.pre == pre && s.post == post && s.is_active < 0.5) {
                s.is_active = 1.0;
                s.w = 0.05 + rand_dist(gen) * 0.1;
                s.prune_timer = 0;
                created_synapses++;
                created_here++;
                found_silent = true;
                break;
            }
        }
        if (found_silent) continue;
        if (existing_conn.count((pre << 16) | post)) continue;

        int pre_layer = neurons[pre].layer_id;
        int post_layer = neurons[post].layer_id;

        bool allowed = false;
        if (pre_layer == 0 && post_layer == 1) allowed = true;      // Sensory → Hidden
        else if (pre_layer == 0 && post_layer == 6) allowed = true; // Sensory → EC
        else if (pre_layer == 1 && post_layer == 4) allowed = true; // Hidden → Thalamus
        else if (pre_layer == 4 && post_layer == 3) allowed = true; // Thalamus → PFC
        else if (pre_layer == 3 && post_layer == 2) allowed = true; // PFC → Motor
        else if (pre_layer == 3 && post_layer == 6) allowed = true; // PFC → EC
        else if (pre_layer == 6 && post_layer == 3) allowed = true; // EC → PFC
        else if (pre_layer == 6 && post_layer == 5) allowed = true; // EC → Hippocampus (perforant path)
        else if (pre_layer == 5 && post_layer == 6) allowed = true; // Hippocampus → EC (feedback)
        else if (pre_layer == 5 && post_layer == 3) allowed = true; // Hippocampus → PFC
        else if (pre_layer == 1 && post_layer == 1) allowed = true; // Hidden recurrent
        else if (pre_layer == 3 && post_layer == 3) allowed = true; // PFC recurrent
        else if (pre_layer == 2 && post_layer == 2) allowed = true; // Motor recurrent
        else if (pre_layer == 5 && post_layer == 5) allowed = true; // Hippocampus recurrent
        else if (pre_layer == 6 && post_layer == 6) allowed = true; // EC recurrent
        if (!allowed) continue;

        // Distancia 3D
        double dx = neurons[pre].x - neurons[post].x;
        double dy = neurons[pre].y - neurons[post].y;
        double dz = neurons[pre].z - neurons[post].z;
        double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist >= 30.0) continue;

        Synapse s;
        s.pre = pre;
        s.post = post;
        s.w = 0.05 + rand_dist(gen) * 0.1;
        s.myelination = 0.1;
        s.is_excitatory = (neurons[pre].type == 1) ? 1.0 : 0.0;
        s.target_is_dendrite = (rand_dist(gen) < 0.5) ? 1.0 : 0.0;
        s.is_active = 1.0;
        s.dendritic_branch = (int)(rand_dist(gen) * 3);
        s.w_early = 0.0;
        s.w_late = 0.0;
        s.tag = 0.0;
        s.U_stp = (neurons[pre].type == 1) ? 0.12 : 0.5;
        s.tau_d = (neurons[pre].type == 1) ? 80.0 : 100.0;
        s.tau_f = (neurons[pre].type == 1) ? 100.0 : 150.0;
        s.x_stp = 1.0;
        s.u_stp = s.U_stp;
        s.apre = 0.0;
        s.apre2 = 0.0;
        s.apost = 0.0;
        s.last_update_ms = 0.0;
        s.prune_timer = 0;
        s.base_delay_ms = dist;
        s.delay_steps = (int)std::round(dist / (1.0 + 3.0 * s.myelination));
        if (s.delay_steps < 1) s.delay_steps = 1;
        if (s.delay_steps > 15) s.delay_steps = 15;

        synapses.push_back(s);
        dist_3d.push_back(dist);
        existing_conn.insert((pre << 16) | post);
        created_synapses++;
        created_here++;
        syn_lists_built = false;
    }
}

void BrainUnico::sleep_replay() {
    // Replay de memoria episódica durante REM
    if (episodic_buffer.empty()) return;
    
    int replay_count = std::min((int)episodic_buffer.size(), 10);
    for (int r = 0; r < replay_count; ++r) {
        int idx = (int)(rand_dist(gen) * episodic_buffer.size());
        const auto& mem = episodic_buffer[idx];
        
        // Reactivar patrón sensorial con ruido reducido
        for (int i = 0; i < N_SENSORY && i < (int)mem.sensory.size(); ++i) {
            neurons[i].I_ext += mem.sensory[i] * 0.3;
        }
    }
}

bool BrainUnico::save_state(const std::string& filepath) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;
    
    // Guardar estado básico
    ofs.write(reinterpret_cast<const char*>(&time_ms), sizeof(time_ms));
    ofs.write(reinterpret_cast<const char*>(&step_count), sizeof(step_count));
    
    // Guardar brain_state
    size_t state_len = brain_state.size() + 1;
    ofs.write(reinterpret_cast<const char*>(&state_len), sizeof(state_len));
    ofs.write(brain_state.c_str(), state_len);
    
    // Guardar neuronas
    size_t n_size = neurons.size();
    ofs.write(reinterpret_cast<const char*>(&n_size), sizeof(n_size));
    for (const auto& n : neurons) {
        ofs.write(reinterpret_cast<const char*>(&n), sizeof(Neuron));
    }
    
    // Guardar sinapsis
    size_t s_size = synapses.size();
    ofs.write(reinterpret_cast<const char*>(&s_size), sizeof(s_size));
    for (const auto& s : synapses) {
        ofs.write(reinterpret_cast<const char*>(&s), sizeof(Synapse));
    }
    
    // Guardar buffers de conductancias
    for (int i = 0; i < N_TOTAL; ++i) {
        ofs.write(reinterpret_cast<const char*>(ring_ampa_soma[i]), 16 * sizeof(double));
        ofs.write(reinterpret_cast<const char*>(ring_gaba_soma[i]), 16 * sizeof(double));
        for (int b = 0; b < 3; ++b) {
            ofs.write(reinterpret_cast<const char*>(ring_ampa_dend_branch[i][b]), 16 * sizeof(double));
            ofs.write(reinterpret_cast<const char*>(ring_gaba_dend_branch[i][b]), 16 * sizeof(double));
        }
    }
    ofs.write(reinterpret_cast<const char*>(&current_ring_step), sizeof(current_ring_step));
    
    // Guardar neuromoduladores
    ofs.write(reinterpret_cast<const char*>(&neuromod), sizeof(NeuromodulatorSystem));
    
    // Guardar estado escalar
    ofs.write(reinterpret_cast<const char*>(&cortisol), sizeof(cortisol));
    ofs.write(reinterpret_cast<const char*>(&ncc_chaos_state), sizeof(ncc_chaos_state));
    ofs.write(reinterpret_cast<const char*>(&melatonina), sizeof(melatonina));
    ofs.write(reinterpret_cast<const char*>(&last_state_value), sizeof(last_state_value));
    ofs.write(reinterpret_cast<const char*>(&alpha_value), sizeof(alpha_value));
    ofs.write(reinterpret_cast<const char*>(&frustration), sizeof(frustration));
    ofs.write(reinterpret_cast<const char*>(&resilience), sizeof(resilience));
    
    // Guardar pfc_gates, w_value
    ofs.write(reinterpret_cast<const char*>(pfc_gates), N_TOTAL * sizeof(double));
    ofs.write(reinterpret_cast<const char*>(w_value), N_TOTAL * sizeof(double));
    
    // Guardar scaler y gain_control
    ofs.write(reinterpret_cast<const char*>(&scaler), sizeof(SynapticScaler));
    ofs.write(reinterpret_cast<const char*>(&gain_control), sizeof(GainController));
    
    // Guardar siamese state
    ofs.write(reinterpret_cast<const char*>(&siamese_margin), sizeof(siamese_margin));
    ofs.write(reinterpret_cast<const char*>(&siamese_lr), sizeof(siamese_lr));
    ofs.write(reinterpret_cast<const char*>(&siamese_trace_decay), sizeof(siamese_trace_decay));
    ofs.write(reinterpret_cast<const char*>(&siamese_genuine_similarity), sizeof(siamese_genuine_similarity));
    ofs.write(reinterpret_cast<const char*>(&siamese_impostor_similarity), sizeof(siamese_impostor_similarity));
    size_t n_sp = siamese_pairs.size();
    ofs.write(reinterpret_cast<const char*>(&n_sp), sizeof(n_sp));
    for (const auto& sp : siamese_pairs) {
        ofs.write(reinterpret_cast<const char*>(&sp), sizeof(SiamesePair));
    }
    
    // Guardar v_if
    ofs.write(reinterpret_cast<const char*>(v_if), N_SENSORY * sizeof(double));
    
    return true;
}

bool BrainUnico::load_state(const std::string& filepath) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) return false;
    
    // Cargar estado básico
    ifs.read(reinterpret_cast<char*>(&time_ms), sizeof(time_ms));
    ifs.read(reinterpret_cast<char*>(&step_count), sizeof(step_count));
    
    // Cargar brain_state
    size_t state_len;
    ifs.read(reinterpret_cast<char*>(&state_len), sizeof(state_len));
    std::vector<char> buf(state_len);
    ifs.read(buf.data(), state_len);
    brain_state = std::string(buf.data());
    
    // Cargar neuronas
    size_t n_size;
    ifs.read(reinterpret_cast<char*>(&n_size), sizeof(n_size));
    if (n_size != neurons.size()) {
        neurons.resize(n_size);
    }
    for (size_t i = 0; i < n_size; ++i) {
        ifs.read(reinterpret_cast<char*>(&neurons[i]), sizeof(Neuron));
    }
    
    // Cargar sinapsis
    size_t s_size;
    ifs.read(reinterpret_cast<char*>(&s_size), sizeof(s_size));
    if (s_size != synapses.size()) {
        synapses.resize(s_size);
        dist_3d.resize(s_size);
    }
    for (size_t i = 0; i < s_size; ++i) {
        ifs.read(reinterpret_cast<char*>(&synapses[i]), sizeof(Synapse));
    }
    
    // Cargar buffers de conductancias
    for (int i = 0; i < N_TOTAL; ++i) {
        ifs.read(reinterpret_cast<char*>(ring_ampa_soma[i]), 16 * sizeof(double));
        ifs.read(reinterpret_cast<char*>(ring_gaba_soma[i]), 16 * sizeof(double));
        for (int b = 0; b < 3; ++b) {
            ifs.read(reinterpret_cast<char*>(ring_ampa_dend_branch[i][b]), 16 * sizeof(double));
            ifs.read(reinterpret_cast<char*>(ring_gaba_dend_branch[i][b]), 16 * sizeof(double));
        }
    }
    ifs.read(reinterpret_cast<char*>(&current_ring_step), sizeof(current_ring_step));
    
    // Cargar neuromoduladores
    ifs.read(reinterpret_cast<char*>(&neuromod), sizeof(NeuromodulatorSystem));
    
    // Cargar estado escalar
    ifs.read(reinterpret_cast<char*>(&cortisol), sizeof(cortisol));
    ifs.read(reinterpret_cast<char*>(&ncc_chaos_state), sizeof(ncc_chaos_state));
    ifs.read(reinterpret_cast<char*>(&melatonina), sizeof(melatonina));
    ifs.read(reinterpret_cast<char*>(&last_state_value), sizeof(last_state_value));
    ifs.read(reinterpret_cast<char*>(&alpha_value), sizeof(alpha_value));
    ifs.read(reinterpret_cast<char*>(&frustration), sizeof(frustration));
    ifs.read(reinterpret_cast<char*>(&resilience), sizeof(resilience));
    
    // Cargar pfc_gates, w_value
    ifs.read(reinterpret_cast<char*>(pfc_gates), N_TOTAL * sizeof(double));
    ifs.read(reinterpret_cast<char*>(w_value), N_TOTAL * sizeof(double));
    
    // Cargar scaler y gain_control
    ifs.read(reinterpret_cast<char*>(&scaler), sizeof(SynapticScaler));
    ifs.read(reinterpret_cast<char*>(&gain_control), sizeof(GainController));
    
    // Cargar siamese state
    ifs.read(reinterpret_cast<char*>(&siamese_margin), sizeof(siamese_margin));
    ifs.read(reinterpret_cast<char*>(&siamese_lr), sizeof(siamese_lr));
    ifs.read(reinterpret_cast<char*>(&siamese_trace_decay), sizeof(siamese_trace_decay));
    ifs.read(reinterpret_cast<char*>(&siamese_genuine_similarity), sizeof(siamese_genuine_similarity));
    ifs.read(reinterpret_cast<char*>(&siamese_impostor_similarity), sizeof(siamese_impostor_similarity));
    size_t n_sp;
    ifs.read(reinterpret_cast<char*>(&n_sp), sizeof(n_sp));
    siamese_pairs.resize(n_sp);
    for (size_t i = 0; i < n_sp; ++i) {
        ifs.read(reinterpret_cast<char*>(&siamese_pairs[i]), sizeof(SiamesePair));
    }
    
    // Cargar v_if
    ifs.read(reinterpret_cast<char*>(v_if), N_SENSORY * sizeof(double));
    
    // Reconstruir listas de sinapsis
    pre_syn_list.assign(N_TOTAL, {});
    post_syn_list.assign(N_TOTAL, {});
    for (size_t k = 0; k < synapses.size(); ++k) {
        pre_syn_list[synapses[k].pre].push_back((int)k);
        post_syn_list[synapses[k].post].push_back((int)k);
    }
    syn_lists_built = true;
    
    return true;
}

std::string BrainUnico::get_state_json() {
    std::ostringstream oss;
    oss << "{";
    oss << "\"time_ms\":" << time_ms << ",";
    oss << "\"step_count\":" << step_count << ",";
    oss << "\"brain_state\":\"" << brain_state << "\",";
    oss << "\"N_TOTAL\":" << N_TOTAL << ",";
    oss << "\"neurons\":[";
    
    for (int i = 0; i < N_TOTAL; ++i) {
        const auto& n = neurons[i];
        oss << "{";
        oss << "\"id\":" << i << ",";
        oss << "\"v\":" << n.v << ",";
        oss << "\"v_dend\":" << n.v_dend << ",";
        oss << "\"firing_rate\":" << n.firing_rate << ",";
        oss << "\"energy\":" << n.energy << ",";
        oss << "\"layer_id\":" << n.layer_id << ",";
        oss << "\"type\":" << n.type << ",";
        oss << "\"x\":" << n.x << ",";
        oss << "\"y\":" << n.y << ",";
        oss << "\"z\":" << n.z << ",";
        oss << "\"I_ext\":" << n.I_ext;
        oss << "}";
        if (i < N_TOTAL - 1) oss << ",";
    }
    oss << "],";
    
    // Sinapsis activas para visualizador 3D (hasta 400 sinapsis más fuertes para renderizado fluido)
    oss << "\"synapses\":[";
    int syn_count = 0;
    for (size_t k = 0; k < synapses.size() && syn_count < 400; ++k) {
        const auto& s = synapses[k];
        if (s.is_active > 0.5 && s.w > 0.2) {
            if (syn_count > 0) oss << ",";
            oss << "{\"pre\":" << s.pre << ",\"post\":" << s.post << ",\"w\":" << s.w << ",\"exc\":" << (s.is_excitatory > 0.5 ? "true" : "false") << "}";
            syn_count++;
        }
    }
    oss << "],";

    // Sinapsis activas total
    int active_syn = 0;
    for (const auto& s : synapses) if (s.is_active > 0.5) active_syn++;
    oss << "\"active_synapses\":" << active_syn << ",";
    
    // Neuromoduladores
    oss << "\"neuromod\":{";
    oss << "\"dopamine\":" << neuromod.dopamine << ",";
    oss << "\"serotonin\":" << neuromod.serotonin << ",";
    oss << "\"acetylcholine\":" << neuromod.acetylcholine;
    oss << "},";
    
    // Métricas y alias de compatibilidad
    double current_w_mean = history.empty() ? 0.0 : history.back().w_mean;
    double current_energy_mean = history.empty() ? 0.0 : history.back().energy_mean;
    oss << "\"w_mean\":" << current_w_mean << ",";
    oss << "\"energy_mean\":" << current_energy_mean << ",";
    oss << "\"spikes\":" << spikes_in_current_batch << ",";
    oss << "\"frustration\":" << frustration << ",";
    oss << "\"siamese_genuine\":" << siamese_genuine_similarity << ",";
    oss << "\"siamese_impostor\":" << siamese_impostor_similarity << ",";

    // === TELEMETRÍA DE NUEVOS MÓDULOS BIOLÓGICOS (v2.5) ===
    const auto& sp_target = spatial_parietal.get_last_target();
    double doppler_c = (N_TOTAL > 127) ? (neurons[127].I_ext * 0.1) : 0.0;
    double radial_velocity_mps = doppler_c * 0.0615; // Velocidad radial estimada en m/s

    oss << "\"spatial_target\":{";
    oss << "\"angle_deg\":" << sp_target.angle_deg << ",";
    oss << "\"distance_m\":" << sp_target.distance_m << ",";
    oss << "\"x_m\":" << sp_target.x_m << ",";
    oss << "\"y_m\":" << sp_target.y_m << ",";
    oss << "\"velocity_mps\":" << radial_velocity_mps << ",";
    oss << "\"confidence\":" << sp_target.confidence << ",";
    oss << "\"present\":" << (sp_target.target_present ? "true" : "false");
    oss << "},";

    // Diagnósticos de Población Neuronal por Capas (Ingeniería Neuromórfica)
    oss << "\"layer_diagnostics\":[";
    int layer_ids[7] = {0, 1, 4, 3, 2, 6, 5};
    const char* layer_names[7] = {"Sensorial CSI", "Corteza Oculta", "Tálamo", "PFC Memoria", "Motora Decodificador", "Corteza Entorrinal", "Hipocampo"};
    for (int l = 0; l < 7; ++l) {
        int lid = layer_ids[l];
        double sum_fr = 0.0, max_fr = 0.0, sum_v = 0.0;
        int count = 0, silent = 0, saturated = 0;
        for (int i = 0; i < N_TOTAL; ++i) {
            if (neurons[i].layer_id == lid) {
                double fr = neurons[i].firing_rate;
                sum_fr += fr;
                if (fr > max_fr) max_fr = fr;
                if (fr < 1.0) silent++;
                if (fr > 80.0) saturated++;
                sum_v += neurons[i].v;
                count++;
            }
        }
        double mean_fr = count > 0 ? (sum_fr / count) : 0.0;
        double mean_v = count > 0 ? (sum_v / count) : -65.0;
        double silent_pct = count > 0 ? (100.0 * silent / count) : 0.0;
        double sat_pct = count > 0 ? (100.0 * saturated / count) : 0.0;

        if (l > 0) oss << ",";
        oss << "{\"layer_id\":" << lid 
            << ",\"name\":\"" << layer_names[l] << "\""
            << ",\"neurons\":" << count
            << ",\"mean_fr\":" << mean_fr
            << ",\"max_fr\":" << max_fr
            << ",\"mean_v\":" << mean_v
            << ",\"silent_pct\":" << silent_pct
            << ",\"sat_pct\":" << sat_pct << "}";
    }
    oss << "],";

    const auto& bg_dec = basal_ganglia.get_last_decision();
    oss << "\"basal_ganglia\":{";
    oss << "\"action\":" << bg_dec.selected_action << ",";
    oss << "\"label\":\"" << bg_dec.label << "\",";
    oss << "\"confidence\":" << bg_dec.confidence;
    oss << "},";

    oss << "\"cerebellum\":{";
    oss << "\"error\":" << cerebellum.get_mean_error();
    oss << "},";

    // === MOHO MUCILAGINOSO (PHYSARUM) TELEMETRÍA ===
    oss << "\"physarum\":{";
    oss << "\"winning_target\":\"" << physarum.get_winning_target_name() << "\",";
    oss << "\"confidence\":" << physarum.get_winning_confidence() << ",";
    oss << "\"presence_accumulator\":" << physarum.get_presence_accumulator() << ",";
    oss << "\"is_presence_active\":" << (physarum.is_presence_active() ? "true" : "false") << ",";
    oss << "\"active_tubules\":" << physarum.get_active_tubule_count() << ",";
    oss << "\"digested_waste\":" << physarum.get_digested_waste() << ",";
    oss << "\"tubules\":[";
    auto dominant = physarum.get_dominant_tubules(80);
    for (size_t t = 0; t < dominant.size(); ++t) {
        if (t > 0) oss << ",";
        oss << "{\"a\":" << dominant[t].node_a 
            << ",\"b\":" << dominant[t].node_b 
            << ",\"d\":" << dominant[t].conductivity 
            << ",\"f\":" << dominant[t].flux << "}";
    }
    oss << "]},";

    // === SIGNOS VITALES & BIOMEDICINA TELEMETRÍA ===
    oss << "\"vital_signs\":{";
    oss << "\"bpm\":" << vital_signs.bpm << ",";
    oss << "\"rpm\":" << vital_signs.rpm << ",";
    oss << "\"activity\":\"" << vital_signs.activity_label << "\",";
    oss << "\"intensity\":" << vital_signs.activity_intensity << ",";
    oss << "\"waveform_ecg\":[";
    for (size_t i = 0; i < vital_signs.waveform_ecg.size(); ++i) {
        if (i > 0) oss << ",";
        oss << vital_signs.waveform_ecg[i];
    }
    oss << "],\"waveform_resp\":[";
    for (size_t i = 0; i < vital_signs.waveform_resp.size(); ++i) {
        if (i > 0) oss << ",";
        oss << vital_signs.waveform_resp[i];
    }
    oss << "]},";

    // === RED MICELIAL FÚNGICA TELEMETRÍA ===
    oss << "\"mycelium\":{";
    oss << "\"dwell_time_sec\":" << mycelium.get_occupancy_dwell_time_sec() << ",";
    oss << "\"drift_compensation_db\":" << mycelium.get_thermal_drift_compensation() << ",";
    oss << "\"mean_calcium\":" << mycelium.get_mean_calcium() << ",";
    oss << "\"active_hyphae\":" << mycelium.get_active_hyphae_count() << ",";
    oss << "\"nodes\":[";
    const auto& myc_nodes = mycelium.get_nodes();
    for (size_t i = 0; i < myc_nodes.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"x\":" << myc_nodes[i].x 
            << ",\"y\":" << myc_nodes[i].y 
            << ",\"z\":" << myc_nodes[i].z 
            << ",\"v\":" << myc_nodes[i].v 
            << ",\"ca\":" << myc_nodes[i].calcium << "}";
    }
    oss << "],\"cords\":[";
    const auto& myc_cords = mycelium.get_cords();
    int drawn_cords = 0;
    for (size_t i = 0; i < myc_cords.size() && drawn_cords < 80; ++i) {
        if (myc_cords[i].memristance > 0.20) {
            if (drawn_cords > 0) oss << ",";
            oss << "{\"a\":" << myc_cords[i].node_a 
                << ",\"b\":" << myc_cords[i].node_b 
                << ",\"m\":" << myc_cords[i].memristance << "}";
            drawn_cords++;
        }
    }
    oss << "]},";

    // === NUEVOS SUSTRATOS BIO-HÍBRIDOS TELEMETRÍA ===
    oss << "\"plant_priming\":{";
    oss << "\"drift_db\":" << plant_priming.get_environmental_drift_db() << ",";
    oss << "\"osmotic_potential\":" << plant_priming.get_osmotic_potential() << ",";
    oss << "\"is_primed\":" << (plant_priming.is_primed() ? "true" : "false");
    oss << "},";

    oss << "\"auxin_beamformer\":{";
    oss << "\"steered_angle_deg\":" << auxin_beamformer.get_steered_angle_deg() << ",";
    oss << "\"pin_polarization\":" << auxin_beamformer.get_pin_polarization() << ",";
    oss << "\"snr_gain_db\":" << auxin_beamformer.get_beam_snr_gain_db() << ",";
    oss << "\"auxin_left\":" << auxin_beamformer.get_auxin_left() << ",";
    oss << "\"auxin_right\":" << auxin_beamformer.get_auxin_right() << ",";
    oss << "\"is_tracking\":" << (auxin_beamformer.is_tracking() ? "true" : "false");
    oss << "},";

    oss << "\"fungal_quorum\":{";
    oss << "\"occupants\":" << fungal_quorum.get_estimated_occupants() << ",";
    oss << "\"density_index\":" << fungal_quorum.get_crowd_density_index() << ",";
    oss << "\"autoinducer_conc\":" << fungal_quorum.get_autoinducer_concentration() << ",";
    oss << "\"label\":\"" << fungal_quorum.get_occupancy_label() << "\"";
    oss << "}";
    
    // Telemetría del adaptador sensorial
    if (sensor_adapter) {
        std::string s_json = sensor_adapter->get_telemetry_json();
        if (s_json.length() >= 2 && s_json.front() == '{' && s_json.back() == '}') {
            std::string inner = s_json.substr(1, s_json.length() - 2);
            if (!inner.empty()) {
                oss << "," << inner;
            }
        }
        oss << ",\"sensor_source\":\"" << sensor_adapter->get_source_name() << "\"";
    }
    
    oss << "}";
    return oss.str();
}