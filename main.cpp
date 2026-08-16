#include "cerebro.hpp"
#include "server.hpp"
#include "sensor_adapter.hpp"
#include "synthetic_signal_adapter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <fstream>
#include <cstdlib>
#include <iomanip>

std::atomic<bool> sim_running(true);
std::atomic<bool> exit_requested(false);

void signal_handler(int signum) {
    if (sim_running) {
        std::cout << "\n[WARN] Interrupcion detectada. Deteniendo bucle del motor neuromorfico...\n";
        sim_running = false;
    } else {
        std::cout << "\n[WARN] Interrupcion detectada de nuevo. Apagando servidor HTTP...\n";
        exit_requested = true;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    int http_port = 8000;
    int max_steps = -1;
    bool disable_server = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            http_port = std::atoi(argv[++i]);
        } else if (arg == "--steps" && i + 1 < argc) {
            max_steps = std::atoi(argv[++i]);
        } else if (arg == "--no-server") {
            disable_server = true;
        }
    }

    std::cout << "========================================================================\n";
    std::cout << "  MOTOR NEUROMORFICO UNIVERSAL DE PROCESAMIENTO DE SENALES (C++17)\n";
    std::cout << "  Arquitectura Bio-Hibrida Multirreino (Animal + Plantae + Fungi)\n";
    std::cout << "  Topologia: Sensorial: " << N_SENSORY << ", Oculta: " << N_HIDDEN 
              << ", Motor: " << N_MOTOR << ", PFC: " << N_PFC << " (Total: " << N_TOTAL << " LIF Neurons)\n";
    std::cout << "========================================================================\n";

    // Crear carpeta logs si no existe
#ifdef _WIN32
    std::system("mkdir logs 2>nul");
#else
    std::system("mkdir -p logs");
#endif

    std::string state_path = "./logs/cerebro_state.bin";

    // 1. Instanciar el adaptador sensorial (Generador Sintetico / Template para Custom Streams)
    auto sensor_adapter = std::make_unique<SyntheticSignalAdapter>(10, 30.0);
    sensor_adapter->connect();

    // 2. Inicializar el nucleo cerebral con el adaptador de sensores
    auto cerebro = std::make_unique<BrainUnico>(std::move(sensor_adapter));

    // Cargar estado previo si existe
    std::ifstream check_file(state_path, std::ios::binary);
    if (check_file.good()) {
        check_file.close();
        cerebro->load_state(state_path);
    }

    // Iniciar servidor HTTP para telemetria y visualizador 3D
    if (!disable_server) {
        start_server(http_port);
    }

    // Bucle principal de computacion bio-hibrida
    auto last_log_time = std::chrono::steady_clock::now();
    int steps_executed = 0;

    std::cout << "[INFO] Motor en ejecucion. Presione Ctrl+C para detener.\n";

    while (sim_running) {
        cerebro->step();
        steps_executed++;

        if (max_steps > 0 && steps_executed >= max_steps) {
            std::cout << "\n[INFO] Limite de " << max_steps << " pasos alcanzado. Guardando estado y finalizando...\n";
            sim_running = false;
            break;
        }

        // Telemetria en consola cada 500 ms de tiempo real
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time).count();
        if (elapsed_ms >= 500) {
            last_log_time = now;
            if (!cerebro->history.empty()) {
                const auto& h = cerebro->history.back();
                std::string source_tag = cerebro->sensor_adapter ? cerebro->sensor_adapter->get_source_name() : "UNIVERSAL_STREAM";

                std::cout << "[Paso " << cerebro->step_count << "] t=" << std::fixed << std::setprecision(1) << (cerebro->time_ms / 1000.0) << "s | " 
                          << cerebro->brain_state 
                          << " | Fuente: " << source_tag
                          << " | W=" << std::setprecision(3) << h.w_mean 
                          << " | DA=" << std::setprecision(2) << h.da 
                          << " | E=" << (int)(h.energy_mean * 100) << "%"
                          << " | Spikes=" << h.spikes;

                // Metricas de adaptacion biologica
                std::cout << " | Quorum: " << cerebro->fungal_quorum.get_estimated_occupants() 
                          << " (" << cerebro->fungal_quorum.get_occupancy_label() << ")"
                          << " | Auxinas Gain: +" << std::setprecision(2) << cerebro->auxin_beamformer.get_beam_snr_gain_db() << "dB"
                          << " | Physarum: " << cerebro->physarum.get_winning_target_name();

                std::cout << "\n" << std::flush;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Persistir estado sinaptico y morfologico
    cerebro->save_state(state_path);
    std::cout << "[INFO] Estado persistido en " << state_path << ".\n";

    if (!disable_server) {
        std::cout << "[INFO] Motor pausado. Presione Ctrl+C de nuevo para apagar el servidor HTTP...\n";
        while (!exit_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        stop_server();
        std::cout << "[INFO] Servidor HTTP detenido.\n";
    }

    std::cout << "[INFO] Apagado limpio completado exitosamente.\n";
    return 0;
}
