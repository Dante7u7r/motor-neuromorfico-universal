#ifndef SENSOR_ADAPTER_HPP
#define SENSOR_ADAPTER_HPP

#include <vector>
#include <string>
#include <memory>

enum class SensorSourceType {
    WIFI_CSI,
    BIOSIGNAL_EEG_EMG,
    ACOUSTIC_RADAR,
    SYNTHETIC_BENCHMARK
};

// Marco sensorial genérico y universal
struct SensoryFrame {
    // 128 canales sensoriales normalizados para la capa de entrada de la SNN
    std::vector<double> channels;
    
    // Perfil espectral / canales de frecuencia (64 bandas)
    std::vector<float> spectrum_amps;
    
    // Gradientes espaciales o diferencias de fase (63 canales)
    std::vector<float> spatial_gradients;
    
    // Componente cinemático o centroide Doppler / velocidad
    double motion_velocity;
    
    // Calidad de la señal o relación Señal/Ruido normalizada [0.0, 1.0]
    double signal_quality;
    
    // Tipo de sensor y etiquetas de estado
    SensorSourceType source_type;
    std::string source_name;
    std::string status_label;
    
    // Contadores de flujo de paquetes / muestras
    int primary_samples_count;
    int secondary_samples_count;

    SensoryFrame()
        : channels(128, 0.0),
          spectrum_amps(64, 0.0f),
          spatial_gradients(63, 0.0f),
          motion_velocity(0.0),
          signal_quality(1.0),
          source_type(SensorSourceType::SYNTHETIC_BENCHMARK),
          source_name("GENERIC_STREAM"),
          status_label("OK"),
          primary_samples_count(0),
          secondary_samples_count(0) {}
};

// Interfaz abstracta para cualquier adaptador de entrada sensorial
class ISensorAdapter {
public:
    virtual ~ISensorAdapter() = default;

    // Inicialización y desconexión
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Lee el cuadro sensorial actual y actualiza su dinámica temporal
    virtual bool read_sensory_frame(SensoryFrame& frame, double dt_sec, double time_ms) = 0;

    // Envía retroalimentación motora al actuador/ambiente
    virtual void send_motor_feedback(const std::vector<double>& motor_firing_rates, double time_ms) = 0;

    // Genera telemetría JSON específica del adaptador
    virtual std::string get_telemetry_json() const = 0;

    // Metadatos de la fuente
    virtual std::string get_source_name() const = 0;
    virtual SensorSourceType get_source_type() const = 0;

    // Señal de recompensa por refuerzo (opcional)
    virtual double get_reward() { return 0.0; }

    // Retorna si el adaptador está en modo de calibración / mapeo
    virtual bool is_calibrating() const { return false; }
};

#endif // SENSOR_ADAPTER_HPP
