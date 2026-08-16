#ifndef MYCELIUM_SUBSTRATE_HPP
#define MYCELIUM_SUBSTRATE_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdlib>

// Forward declarations
struct Neuron;

/**
 * ============================================================================
 * MYCELIUM SUBSTRATE (COMPUTACIÓN MICELIAL FÚNGICA & RED DE ANASTOMOSIS)
 * ============================================================================
 * Modelo biofísico de red fúngica multicelular excitable basado en las
 * ecuaciones de FitzHugh-Nagumo acopladas espacialmente y memristancia biológica
 * (Adamatzky et al., Unconventional Computing Lab, 2021-2024).
 *
 * Funciones clave en Cerebro SNN:
 * 1. Anti-Drift RF: Filtro basal continuo para deriva térmica en ESP32 (τ = 60-300s).
 * 2. Memoria de Permanencia (Dwell-Time): Integral de tiempo de ocupación macro.
 * 3. Wood-Wide Web: Distribución homeostática de energía entre capas fatigadas.
 * 4. Memristancia por Histéresis: Red de anastomosis auto-reconfigurable.
 */

class MyceliumSubstrate {
public:
    struct HyphaNode {
        double v;           // Potencial eléctrico de membrana (-1.0 a +1.5 adim)
        double w;           // Variable lenta de recuperación (eflujo de Ca2+/K+)
        double calcium;     // Concentración de calcio intracelular (0.0 a 1.0)
        double energy_pool; // Reserva energética de biomasa para neuronas
        double x, y, z;     // Coordenadas espaciales 3D en la base
        int target_layer;   // Capa anatómica a la que se ancla (-1 = base pura)
    };

    struct HyphalCord {
        int node_a;
        int node_b;
        double memristance; // Conductancia memristiva M_ij (0.05 a 2.5)
        double length;      // Distancia euclidiana
    };

    MyceliumSubstrate(int n_nodes = 64) 
        : n_nodes_(n_nodes),
          c1_(1.0), c2_(1.0), a_(0.15), b_(0.8),
          epsilon_(0.003), // Escala temporal ultra-lenta (minutos)
          d_diff_(0.12),
          eta_(0.02), beta_(0.005), m0_(0.25),
          dwell_time_sec_(0.0),
          thermal_drift_offset_db_(0.0) {
        build_3d_anastomosis_mesh();
    }

    void build_3d_anastomosis_mesh() {
        nodes_.resize(n_nodes_);
        cords_.clear();

        // 1. Distribuir nodos en la base inferior del cerebro (z in [-25, -5])
        int grid_size = static_cast<int>(std::sqrt(n_nodes_));
        if (grid_size < 1) grid_size = 1;
        double spacing = 18.0;

        for (int i = 0; i < n_nodes_; ++i) {
            int gx = i % grid_size - grid_size / 2;
            int gy = (i / grid_size) - grid_size / 2;
            double rx = gx * spacing + ((rand() % 100) / 100.0 - 0.5) * 4.0;
            double ry = gy * spacing + ((rand() % 100) / 100.0 - 0.5) * 4.0;
            double rz = -22.0 + ((rand() % 100) / 100.0) * 8.0;

            nodes_[i].v = -0.1;
            nodes_[i].w = 0.0;
            nodes_[i].calcium = 0.05;
            nodes_[i].energy_pool = 1.0;
            nodes_[i].x = rx;
            nodes_[i].y = ry;
            nodes_[i].z = rz;
            nodes_[i].target_layer = i % 7; // Asignación proporcional a las 7 capas
        }

        // 2. Crear cordones miceliales mediante Anastomosis (K-vecinos más cercanos)
        for (int i = 0; i < n_nodes_; ++i) {
            for (int j = i + 1; j < n_nodes_; ++j) {
                double dx = nodes_[i].x - nodes_[j].x;
                double dy = nodes_[i].y - nodes_[j].y;
                double dz = nodes_[i].z - nodes_[j].z;
                double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                // Conectar si la distancia es menor a un radio umbral
                if (dist < 26.0) {
                    HyphalCord cord;
                    cord.node_a = i;
                    cord.node_b = j;
                    cord.memristance = m0_;
                    cord.length = std::max(1.0, dist);
                    cords_.push_back(cord);
                }
            }
        }
    }

    /**
     * Paso de integración temporal de la red fúngica
     */
    template <typename NeuronType>
    void step_dynamics(double dt_sec, 
                       double ambient_csi_energy, 
                       double ambient_doppler, 
                       bool target_present,
                       std::vector<NeuronType>& neurons) {
        if (nodes_.empty()) return;

        // 1. Estimación continua de deriva térmica (Filtro pasa-bajas de tiempo largo)
        double target_drift = ambient_csi_energy * 0.05;
        thermal_drift_offset_db_ = 0.998 * thermal_drift_offset_db_ + 0.002 * target_drift;

        // 2. Acumulación de Dwell-Time (Permanencia macro en sala)
        if (target_present) {
            dwell_time_sec_ += dt_sec;
        } else {
            dwell_time_sec_ = std::max(0.0, dwell_time_sec_ - dt_sec * 0.25);
        }

        // 3. Ecuaciones de FitzHugh-Nagumo sobre la malla de hifas
        std::vector<double> dv(n_nodes_, 0.0);
        std::vector<double> dw(n_nodes_, 0.0);

        // Aporte de difusión a través de cordones miceliales
        for (const auto& cord : cords_) {
            int a = cord.node_a;
            int b = cord.node_b;
            double flux = cord.memristance * (nodes_[b].v - nodes_[a].v) / cord.length;
            dv[a] += d_diff_ * flux;
            dv[b] -= d_diff_ * flux;
        }

        // Estimulación ambiental distribuida
        double input_stim = target_present ? (0.25 + 0.15 * std::abs(ambient_doppler)) : 0.02;

        for (int i = 0; i < n_nodes_; ++i) {
            double v = nodes_[i].v;
            double w = nodes_[i].w;

            // FHN no-lineal
            double nonlin = c1_ * v * (v - a_) * (1.0 - v) - c2_ * w + input_stim + dv[i];
            double recov = epsilon_ * (v - b_ * w);

            nodes_[i].v = std::max(-0.5, std::min(1.5, v + nonlin * dt_sec * 0.5));
            nodes_[i].w = std::max(0.0, std::min(1.0, w + recov * dt_sec * 0.5));

            // Concentración de calcio acoplada a la despolarización
            nodes_[i].calcium = 0.995 * nodes_[i].calcium + 0.005 * std::max(0.0, nodes_[i].v);
        }

        // 4. Memristancia Fúngica (Histéresis en los cordones)
        for (auto& cord : cords_) {
            double v_diff = std::abs(nodes_[cord.node_a].v - nodes_[cord.node_b].v);
            cord.memristance += (eta_ * v_diff - beta_ * (cord.memristance - m0_)) * dt_sec * 0.1;
            cord.memristance = std::max(0.05, std::min(2.5, cord.memristance));
        }

        // 5. Wood-Wide Web: Transferencia homeostática de recursos metabólicos
        transfer_metabolic_energy(neurons);
    }

    /**
     * Transferencia de reservas metabólicas desde el micelio hacia neuronas fatigadas
     */
    template <typename NeuronType>
    void transfer_metabolic_energy(std::vector<NeuronType>& neurons) {
        if (neurons.empty() || nodes_.empty()) return;

        for (int i = 0; i < n_nodes_; ++i) {
            int lid = nodes_[i].target_layer;
            double node_calcium = nodes_[i].calcium;

            // Buscar neuronas en la capa asociada
            for (size_t n = 0; n < neurons.size(); ++n) {
                // Seleccionar muestra proporcional
                if (n % n_nodes_ == static_cast<size_t>(i)) {
                    // Si la neurona está agotada (energía < 0.25), el micelio le inyecta biomasa
                    if (neurons[n].energy < 0.25 && nodes_[i].energy_pool > 0.1) {
                        double boost = 0.015 * (1.0 + node_calcium);
                        neurons[n].energy = std::min(1.0, static_cast<double>(neurons[n].energy) + boost);
                        nodes_[i].energy_pool = std::max(0.05, nodes_[i].energy_pool - boost * 0.5);
                    } else if (neurons[n].energy > 0.85) {
                        // Recargar reserva micelial
                        nodes_[i].energy_pool = std::min(1.0, nodes_[i].energy_pool + 0.005);
                    }
                }
            }
        }
    }

    // Getters para Cerebro
    double get_thermal_drift_compensation() const { return thermal_drift_offset_db_; }
    double get_occupancy_dwell_time_sec() const { return dwell_time_sec_; }
    
    double get_mean_calcium() const {
        if (nodes_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& n : nodes_) sum += n.calcium;
        return sum / nodes_.size();
    }

    int get_active_hyphae_count() const {
        int count = 0;
        for (const auto& c : cords_) {
            if (c.memristance > 0.35) count++;
        }
        return count;
    }

    const std::vector<HyphaNode>& get_nodes() const { return nodes_; }
    const std::vector<HyphalCord>& get_cords() const { return cords_; }

private:
    int n_nodes_;
    double c1_, c2_, a_, b_;
    double epsilon_;
    double d_diff_;
    double eta_, beta_, m0_;

    double dwell_time_sec_;
    double thermal_drift_offset_db_;

    std::vector<HyphaNode> nodes_;
    std::vector<HyphalCord> cords_;
};

#endif // MYCELIUM_SUBSTRATE_HPP
