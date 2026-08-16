#ifndef CEREBRO_HPP
#define CEREBRO_HPP

#include "sensor_adapter.hpp"
#include "spatial_parietal.hpp"
#include "cerebellar_model.hpp"
#include "basal_ganglia.hpp"
#include "physarum_optimizer.hpp"
#include "mycelium_substrate.hpp"
#include "bio_hybrid_plant_fungi.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <deque>

// Constantes globales de topología (Cerebro 2.0 - 140 Neuronas base)
const int N_SENSORY = 128;       // 5 CSI streams: Amp(64) + PhaseDiff(63) + Ratio(63) + Doppler(10) + Stats(8) = 208 → comprimido a 128
const int N_HIDDEN  = 50;        // 5 hidden streams paralelos: 10 cada uno (Amp, Phase, Ratio, Doppler, Stats)
const int N_TALAMUS = 10;
const int N_PFC     = 26;        // Fase 2: PFC-VERIFY(10), PFC-IDENTIFY(10), PFC-GATE(4), PFC-WTA(2) = 26
const int N_MOTOR   = 30;        // Motor layer 80-109, zonas dinámicas asignadas via bridge
const int N_EC      = 10;        // Corteza Entorrinal (puente Sensory → Hipocampo)
const int N_HIPO    = 20;
const int N_TOTAL   = N_SENSORY + N_HIDDEN + N_TALAMUS + N_PFC + N_MOTOR + N_EC + N_HIPO; // 274

// Índices PFC Split (Fase 2)
const int PFC_VERIFY_START    = N_SENSORY + N_HIDDEN + N_TALAMUS;           // 128+50+10 = 188
const int PFC_VERIFY_END      = PFC_VERIFY_START + 10;                      // 198 (10 neuronas)
const int PFC_IDENTIFY_START  = PFC_VERIFY_END;                             // 198
const int PFC_IDENTIFY_END    = PFC_IDENTIFY_START + 10;                    // 208 (10 neuronas)
const int PFC_GATE_START      = PFC_IDENTIFY_END;                           // 208
const int PFC_GATE_END        = PFC_GATE_START + 4;                         // 212 (4 neuronas)
const int PFC_WTA_START       = PFC_GATE_END;                               // 212
const int PFC_WTA_END         = PFC_WTA_START + 2;                          // 214 (2 neuronas)
const int PFC_TOTAL           = PFC_WTA_END - PFC_VERIFY_START;             // 26

// Índices Hidden Streams (Fase 3) - 5 streams paralelos de 10 neuronas cada uno
const int HIDDEN_AMP_START    = N_SENSORY;                              // 128
const int HIDDEN_AMP_END      = HIDDEN_AMP_START + 10;                  // 138
const int HIDDEN_PHASE_START  = HIDDEN_AMP_END;                         // 138
const int HIDDEN_PHASE_END    = HIDDEN_PHASE_START + 10;                // 148
const int HIDDEN_RATIO_START  = HIDDEN_PHASE_END;                       // 148
const int HIDDEN_RATIO_END    = HIDDEN_RATIO_START + 10;                // 158
const int HIDDEN_DOPPLER_START = HIDDEN_RATIO_END;                      // 158
const int HIDDEN_DOPPLER_END  = HIDDEN_DOPPLER_START + 10;              // 168
const int HIDDEN_STATS_START  = HIDDEN_DOPPLER_END;                     // 168
const int HIDDEN_STATS_END    = HIDDEN_STATS_START + 10;                // 178
const int HIDDEN_TOTAL        = HIDDEN_STATS_END - N_SENSORY;           // 50

const double DT = 1.0; // ms
const double BATCH_MS = 500.0;
const double SLEEP_CYCLE_PERIOD = 20000.0;
const double SIGNAL_PERIOD = 127.7;

struct Neuron {
    double v;
    double v_dend;
    double v_thresh;
    double v_thresh_base;
    double tau_m; // ms
    double tau_dend; // ms
    double g_coupling;
    double E_ampa;
    double E_gaba;
    double v_rest;
    
    // Conductancias dinámicas
    double g_ampa_soma;
    double g_gaba_soma;
    double g_ampa_dend;
    double g_gaba_dend;

    // Conductancias dinámicas dendríticas compartimentales (Rama 0=Sujeto A, 1=Sujeto B, 2=Multitud/Otros)
    double g_ampa_dend_branch[3];
    double g_gaba_dend_branch[3];

    // Umbral Adaptativo
    double delta_v_thresh;
    double tau_thresh; // ms

    // Ruido y CPG
    double noise_base;
    double is_sensory;
    double cpg_amplitude;
    double signal_period; // ms

    // Metabolismo y Emociones
    double energy;
    double firing_rate; // Hz
    double last_spike_time; // segundos
    double frustration;
    double resilience;

    // Neuromoduladores acoplados
    double da;
    double ser;
    double ach;

    // Físico / Red
    int layer_id; // 0=Sensorial, 1=Oculta, 2=Motor, 3=PFC, 4=Tálamos, 5=Hipocampo
    int type;     // 1=Excitatoria, 4=Inhibitoria
    double x, y, z;
    double I_ext;
};

struct Synapse {
    int pre;
    int post;
    double w;
    double myelination;
    double is_excitatory;     // 1.0 o 0.0
    double target_is_dendrite; // 1.0 o 0.0
    double is_active;          // 1.0 o 0.0
    int dendritic_branch;      // Compartimento dendrítico: 0, 1, 2

    // Consolidadación Synaptic Tagging and Capture (STC)
    double w_early;           // Fase temprana (STDP / E-LTP)
    double w_late;            // Fase tardía consolidada (L-LTP)
    double tag;               // Etiqueta de plasticidad sináptica

    // STP (Tsodyks-Markram)
    double x_stp;
    double u_stp;
    double U_stp;
    double tau_d; // ms
    double tau_f; // ms

    // STDP (triplete: apre rápida, apre2 lenta, apost para LTD)
    double apre;
    double apre2;
    double apost;
    double last_update_ms;

    // Delays
    int delay_steps;
    double base_delay_ms;

    // Poda homeostática
    int prune_timer;
};

struct Astrocyte {
    int group_start;
    int group_end;
    double calcium;
};

// ============================================================================
// PCA ONLINE PARA COMPRESIÓN DE FEATURES CSI (Fase 3)
// ============================================================================
struct OnlinePCA {
    int input_dim;
    int n_components;
    int max_samples;
    std::vector<double> mean;
    std::vector<std::vector<double>> components;
    std::vector<double> explained_variance;
    std::vector<double> singular_values;
    int n_samples_seen = 0;
    bool fitted = false;
    double learning_rate = 0.01;
    
    OnlinePCA() = default;
    OnlinePCA(int input_dim_, int n_components_ = 16, int max_samples_ = 5000, double lr = 0.01);
    
    void reset(int new_input_dim = -1, int new_n_components = -1);
    void partial_fit(const std::vector<double>& x);
    std::vector<double> transform(const std::vector<double>& x) const;
    std::vector<double> inverse_transform(const std::vector<double>& proj) const;
    double get_explained_variance_ratio(int k) const;
};

class NeuromodulatorSystem {
public:
    double dopamine;
    double serotonin;
    double acetylcholine;
    double norepinephrine;
    double tau_da; // s
    double tau_5ht; // s
    double tau_ach; // s
    double tau_ne; // s

    NeuromodulatorSystem();
    void update(double dt_sec, const std::string& brain_state, double prediction_error);
};

class SynapticScaler {
public:
    double target_sum_w;
    bool active;

    SynapticScaler(double target = 6.0, bool active_val = true);
    void scale(class BrainUnico& brain);
};

class GainController {
public:
    double target_rate;
    double alpha_gain;
    double v_offset;
    bool active;

    GainController(double target = 8.0, double alpha = 0.05, bool active_val = true);
    void adapt(class BrainUnico& brain, double motor_firing);
};

class EspacioGlobal {
public:
    double umbral;
    int ventana;
    double ganancia_broadcast;
    std::deque<double> fr_workspace_history;
    int steps_sobre_umbral;
    bool ignicion_activa;

    EspacioGlobal(double umbral_ini = 30.0, int vent = 100, double ganancia = 0.18);
    std::pair<double, bool> tick(class BrainUnico& brain, const std::vector<double>& firing_rates);
private:
    void adaptar_umbral();
};

struct EpisodicMemory {
    std::vector<double> sensory;
    double time_ms;
};

struct HistoryEntry {
    double time;
    int step;
    char state_code;
    double da;
    double ser;
    double ach;
    double w_mean;
    double w_max;
    int synapses;
    int spikes;
    double energy_mean;
    int pruned;
    int created;
    double prediction;
    double target;
    double workspace_fr;
    double workspace_umbral;
    bool ignicion;
};

class BrainUnico {
public:
    double time_ms;
    int step_count;
    std::string brain_state;
    int pruned_synapses;
    int created_synapses;
    
    double frustration;
    double resilience;
    double decay_factor;

    std::vector<Neuron> neurons;
    std::vector<Synapse> synapses;

    NeuromodulatorSystem neuromod;
    SynapticScaler scaler;
    GainController gain_control;
    EspacioGlobal workspace;
    std::unique_ptr<ISensorAdapter> sensor_adapter;
    SensoryFrame current_sensory_frame;

    // === MÓDULOS BIOLÓGICOS AVANZADOS (Cerebro v2.5 / v3.2) ===
    SpatialParietalCortex spatial_parietal;  // Colículo Superior / Parietal (AoA & Coordenadas 3D)
    CerebellarPredictor cerebellum;          // Cerebelo (Forward Model & Cancelador de Eco/Clutter)
    BasalGangliaCircuit basal_ganglia;       // Ganglios Basales (Vías Go/No-Go & Decisión Estricta)
    PhysarumOptimizer physarum;              // Moho Mucilaginoso (Routing Morfogénico & Garbage Collection)
    MyceliumSubstrate mycelium;              // Red Micelial Fúngica (Sustrato Anti-Drift, Dwell-Time & Homeostasis)

    // === NUEVOS SUSTRATOS BIO-HÍBRIDOS (PLANTAE + FUNGI AVANZADO) ===
    PlantXylemPriming plant_priming;         // Xilema-Floema: Resonador dieléctrico y memoria climática
    AuxinTropismBeamformer auxin_beamformer; // Auxinas PIN: Bio-Beamforming y dirección de lóbulo
    FungalQuorumSensing fungal_quorum;       // Quorum Sensing Fúngico: Cuantificación discreta de aforo (0, 1, 2, 3...)

    // Signos Vitales & Dinámica Fisiológica Extraída por la SNN
    struct VitalSignsState {
        double bpm = 0.0;
        double rpm = 0.0;
        std::string activity_label = "SALA VACIA";
        double activity_intensity = 0.0;
        double ecg_phase = 0.0;
        double resp_phase = 0.0;
        std::vector<double> waveform_ecg;
        std::vector<double> waveform_resp;
        
        VitalSignsState() {
            waveform_ecg.assign(60, 0.0);
            waveform_resp.assign(60, 0.0);
        }
    } vital_signs;

    void update_vital_signs(double dt_sec);

    // Distancias 3D precalculadas en memoria
    std::vector<double> dist_3d;

    // Búferes circulares de conductancias con delays
    double ring_ampa_soma[N_TOTAL][16];
    double ring_ampa_dend_branch[N_TOTAL][3][16];
    double ring_gaba_soma[N_TOTAL][16];
    double ring_gaba_dend_branch[N_TOTAL][3][16];
    int current_ring_step;

    std::deque<EpisodicMemory> episodic_buffer;
    std::vector<HistoryEntry> history;

    // Métricas del step actual
    int spikes_in_current_batch;

    // TD-Learning (Actor-Critic)
    double w_value[N_TOTAL];
    double last_state_value;
    double alpha_value;

    // Gating de Memoria de Trabajo (PBWM)
    double pfc_gates[N_TOTAL];

    // Mapa logístico caótico para el NCC
    double ncc_chaos_state;

    // Capas biológicas multiescala adicionales
    std::vector<Astrocyte> astrocytes;
    double cortisol;
    double melatonina;
    double v_if[N_SENSORY];



    // === SIAMESE STDP PARA BIOMETRÍA CSI (Contrastive Learning) ===
    struct SiamesePair {
        int neuron_a;               // Neurona en rama A (Subject A)
        int neuron_b;               // Neurona en rama B (Subject B / Probe)
        int dendritic_branch_a;     // Rama dendrítica 0=A, 1=B, 2=Crowd
        int dendritic_branch_b;     // Rama dendrítica 0=A, 1=B, 2=Crowd
        double target_similarity;   // 1.0 = same person, 0.0 = different person
        double weight;              // Peso de similitud aprendido
        double trace_a;             // Traza STDP pre-sináptica
        double trace_b;             // Traza STDP post-sináptica
        double margin;              // Margen para contrastive loss
        bool active;                // Si la pareja está activa
        
        SiamesePair() : neuron_a(-1), neuron_b(-1), dendritic_branch_a(0), 
                        dendritic_branch_b(1), target_similarity(0.0), weight(0.0), 
                        trace_a(0.0), trace_b(0.0), margin(1.0), active(false) {}
    };
    
    std::vector<SiamesePair> siamese_pairs;
    double siamese_margin = 0.5;
    double siamese_lr = 0.05;
    double siamese_trace_decay = 2500.0; // Escalado con BATCH_MS: 50 * (500/10)
    
    // Estadísticas para evaluación
    double siamese_genuine_similarity = 0.0;
    double siamese_impostor_similarity = 0.0;

    // Listas de sinapsis para acceso rápido (pre/post)
    std::vector<std::vector<int>> pre_syn_list;
    std::vector<std::vector<int>> post_syn_list;
    bool syn_lists_built = false;

    BrainUnico();
    BrainUnico(std::unique_ptr<ISensorAdapter> sensor);
    void step();
    std::string get_state_json();
    
    bool save_state(const std::string& filepath);
    bool load_state(const std::string& filepath);

private:
    void homeostasis();
    void structural_plasticity();
    void sleep_replay();
    void initialize_siamese_pairs();
    void update_siamese_stdp(const std::vector<bool>& spikes, const std::vector<int>& spike_counts);
};

#endif // CEREBRO_HPP
