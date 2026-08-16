# 🧠 Motor Neuromórfico Universal de Procesamiento de Señales Bio-Híbrido (C++17)

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)]()
[![Zero GPU](https://img.shields.io/badge/Hardware-CPU%20Only%20%28Zero%20GPU%29-brightgreen.svg)]()

Un framework neuromórfico de alto rendimiento y bajo consumo en **C++ nativo puro**, diseñado para el procesamiento y clasificación adaptativa de series temporales y señales biofísicas ruidosas en tiempo real. 

Fusiona la dinámica de redes neuronales pulsantes (**SNN - Spiking Neural Networks**) con modelos computacionales inspirados en **tres reinos biológicos** (*Animalia*, *Protista*, *Fungi* y *Plantae*), logrando auto-calibración, memoria de trabajo, plasticidad continua y homeóstasis sin depender de aceleración por GPU ni marcos de Deep Learning pesados.

---

## 🔬 Arquitectura Bio-Computacional Multirreino

```
                                [ Flujo Sensorial de Entrada ]
                                              │
                      ┌───────────────────────┴───────────────────────┐
                      │                                               │
              [ 🌿 PLANTAE: Xilema ]                         [ 🌱 PLANTAE: Auxinas PIN ]
          Filtro Osmótico Lento (τ = 600s)                Tropismo & Focalización Direccional
          Absorción de Deriva Ambiental                   Aumento de Relación Señal/Ruido
                      │                                               │
                      └───────────────────────┬───────────────────────┘
                                              │
                                   [ 🟡 PROTISTA: Physarum ]
                                Enrutamiento Protoplásmico
                                Digestión de Ruido Residual
                                              │
                                  [ ⚡ ANIMALIA: SNN Core ]
                                274 Neuronas LIF Multicompartimentales
                                Plasticidad STDP + Dopamina/Serotonina
                                Memoria Prefrontal PBWM + Ganglios Basales
                                              │
                                    [ 🍄 FUNGI: Micelio ]
                                Malla Memristiva Anti-Drift
                                Quorum Sensing (Cuantificación de Aforo)
                                              │
                                   [ 📊 Salida de Decisión ]
```

### 1. ⚡ Reino Animalia (Núcleo SNN 274 Neuronas)
* **Neuronas LIF Compartimentales**: Integración con dinámica de fuga, árbol dendrítico ramificado y umbral adaptativo.
* **Plasticidad Sináptica STDP**: Ajuste de pesos pre/post-sináptico acoplado a neuromodulación (Dopamina, Serotonina, Acetilcolina).
* **Circuitos Corticales Especializados**:
  * *Ganglios Basales*: Vías directas (*Go*) e indirectas (*No-Go*) para toma de decisiones nítidas.
  * *Cerebelo Predictivo*: Modelo *forward* para filtrado de clutter y cancelación de señales estáticas.
  * *Homeóstasis de Sueño (SWS)*: Escalamiento sináptico asintótico durante ciclos circadianos de descanso.

### 2. 🟡 Reino Protista (*Physarum polycephalum*)
* Modela la red de tubos protoplásmicos pulsantes del moho mucilaginoso.
* Optimiza los caminos de menor resistencia y flujo de información entre la capa sensorial y la corteza motora, actuando como un *Garbage Collector* biológico que digiere conexiones redundantes.

### 3. 🍄 Reino Fungi (Micelio Memristivo & Quorum Sensing)
* **Memristencia Hifal**: Dinámica no lineal de resistencia dependiente del flujo de carga, previniendo el *drift* y estabilizando el tiempo de permanencia (*dwell-time*).
* **Quorum Sensing**: Difusión y degradación de autoinductores peptídicos en la malla 3D para cuantificación discreta de densidad/aforo.

### 4. 🌿 Reino Plantae (Electrofisiología Vegetal)
* **Red Xilema-Floema**: Flujo vascular de Münch para filtrado osmótico ultra-lento, absorbiendo variaciones de temperatura y línea base.
* **Tropismo de Auxinas**: Transporte polar quimiosmótico mediante polarización de proteínas transmembrana *PIN-FORMED*, actuando como un *beamformer* orgánico que orienta la sensibilidad hacia la fuente principal.

---

## 🔌 Cómo Conectar tus Propias Señales (`ISensorAdapter`)

El motor es completamente **independiente del sensor físico**. Para conectar bioseñales reales (EEG, EMG, ECG), radares mmWave, micrófonos o acelerómetros, solo implementa la interfaz `ISensorAdapter`:

```cpp
#include "sensor_adapter.hpp"

class MiSensorAdapter : public ISensorAdapter {
public:
    bool connect() override {
        // Inicializar puerto serie, Bluetooth, socket TCP o driver
        return true;
    }

    void disconnect() override {}
    bool is_connected() const override { return true; }

    bool read_sensory_frame(SensoryFrame& frame, double dt_sec, double time_ms) override {
        // 1. Llenar los 128 canales de corriente para la SNN
        for (int i = 0; i < 128; ++i) {
            frame.channels[i] = obtener_muestra_canal(i);
        }

        // 2. (Opcional) Llenar espectro de frecuencias (64 bandas)
        frame.spectrum_amps = calcular_fft();

        // 3. (Opcional) Llenar gradientes espaciales
        frame.motion_velocity = obtener_velocidad();

        frame.source_name = "MI_SENSOR_CUSTOM";
        frame.source_type = SensorSourceType::BIOSIGNAL_EEG_EMG;
        return true;
    }

    void send_motor_feedback(const std::vector<double>& motor_rates, double time_ms) override {
        // Enviar respuesta a actuadores / estimuladores
    }

    std::string get_telemetry_json() const override {
        return "{\"custom_data\": 123}";
    }

    std::string get_source_name() const override { return "MI_SENSOR_CUSTOM"; }
    SensorSourceType get_source_type() const override { return SensorSourceType::BIOSIGNAL_EEG_EMG; }
};
```

---

## 🚀 Compilación y Ejecución

### Requisitos:
* Compilador compatible con **C++17** (MSVC 2019+, GCC 9+, Clang 10+).
* CMake 3.15+ (opcional para Linux/macOS).

### En Windows (MSVC):
```powershell
# Compilación rápida nativa
.\build.ps1

# Ejecutar con servidor web activo
.\motor_neuromorfico.exe --port 8000
```

### En Linux / macOS (CMake):
```bash
mkdir build && cd build
cmake ..
make -j4

# Ejecutar
./motor_neuromorfico --port 8000
```

---

## 🌐 Visualizador Web en Tiempo Real

El motor incluye un servidor HTTP ultraligero integrado (sin dependencias externas) que transmite telemetría en tiempo real:

1. Abre tu navegador en: **`http://localhost:8000/demo.html`**
2. Visualiza:
   * **Gemelo 3D de la Red**: Topología espacial de neuronas y túbulos de Physarum en Three.js.
   * **Monitor de Signos Vitales**: Formas de onda y ritmos biológicos extraídos.
   * **Telemetría Plantae & Fungi**: Ganancia de auxinas, potencial osmótico y conteo de quorum.
   * **Endpoint de Telemetría JSON**: `http://localhost:8000/sim_state.json`

---

## 📁 Estructura del Proyecto

```
motor-neuromorfico-universal/
├── sensor_adapter.hpp             # Interfaz abstracta universal ISensorAdapter
├── synthetic_signal_adapter.hpp   # Adaptador de señales sintéticas para benchmarks
├── synthetic_signal_adapter.cpp
├── cerebro.hpp                    # Núcleo SNN 274 LIF, STDP, Aprendizaje por Refuerzo
├── cerebro.cpp
├── bio_hybrid_plant_fungi.hpp     # Sustratos de Xilema, Auxinas PIN y Quorum Sensing
├── mycelium_substrate.hpp         # Malla memristiva fúngica
├── physarum_optimizer.hpp         # Dinámica protoplásmica de Physarum polycephalum
├── basal_ganglia.hpp              # Vías directas e indirectas Go/No-Go
├── cerebellar_model.hpp           # Modelo forward cerebeloso
├── spatial_parietal.hpp           # Mapeo espacial parietal y Place Fields
├── server.hpp                     # Servidor HTTP embebido en Winsock/Sockets
├── server.cpp
├── main.cpp                       # Punto de entrada universal
├── CMakeLists.txt                 # Sistema de construcción multiplataforma
├── build.ps1                      # Script de compilación automática para Windows
└── web_visualizer/                # Dashboard interactivo HTML5 / CSS3 / JavaScript
```

---

## 📄 Licencia

Este proyecto está bajo la Licencia MIT. Libre para uso académico, de investigación y comercial.
