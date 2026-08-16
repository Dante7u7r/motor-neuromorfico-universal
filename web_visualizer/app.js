// CEREBRO SNN v2.0 - Frontend Engine
// Radar Interferométrico Wi-Fi CSI & Red Neuronal 3D en Tiempo Real

let scene, camera, renderer, controls;
let neuronsGroup, synapsesGroup;
let neuronMeshes = [];
let isInitialized = false;
let autoRotate = true;

// Instancia de Chart.js
let neuroChart;
const maxHistoryLen = 30;
const historyData = {
    time: [],
    da: [],
    ser: [],
    ach: []
};

// Paleta Anatómica de Capas (274 Neuronas)
const layerColors = {
    0: 0x00e5ff, // Sensorial CSI (0..127) - Cyan
    1: 0x4466ff, // Oculta (128..177) - Azul Neón
    2: 0xff3366, // Motora (214..243) - Magenta / Rosa Neón
    3: 0xb000ff, // PFC / Memoria (188..213) - Púrpura Neón
    4: 0xffa200, // Tálamo (178..187) - Ámbar
    5: 0x00ff88, // Entorrinal (244..253) - Verde Lima
    6: 0xffea00  // Hipocampo (254..273) - Oro
};

// ============================================================================
// INICIALIZACIÓN THREE.JS 3D
// ============================================================================
function init3D() {
    const container = document.getElementById('canvas-container');
    if (!container) return;

    const width = container.clientWidth || 600;
    const height = container.clientHeight || 450;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x020208);
    scene.fog = new THREE.FogExp2(0x020208, 0.0035);

    camera = new THREE.PerspectiveCamera(45, width / height, 1, 1000);
    camera.position.set(0, 45, 170);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    container.innerHTML = '';
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.06;
    controls.maxDistance = 350;
    controls.minDistance = 30;

    // Iluminación
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
    scene.add(ambientLight);

    const dirLight1 = new THREE.DirectionalLight(0x00e5ff, 0.8);
    dirLight1.position.set(60, 100, 60);
    scene.add(dirLight1);

    const dirLight2 = new THREE.DirectionalLight(0xb000ff, 0.5);
    dirLight2.position.set(-60, -50, -60);
    scene.add(dirLight2);

    neuronsGroup = new THREE.Group();
    synapsesGroup = new THREE.Group();
    physarumGroup = new THREE.Group();
    myceliumGroup = new THREE.Group();
    scene.add(neuronsGroup);
    scene.add(synapsesGroup);
    scene.add(physarumGroup);
    scene.add(myceliumGroup);

    // Resize Handler
    window.addEventListener('resize', onWindowResize);

    // Botones de control
    const btnReset = document.getElementById('btn-reset-cam');
    if (btnReset) {
        btnReset.addEventListener('click', () => {
            camera.position.set(0, 45, 170);
            controls.target.set(0, 0, 0);
            controls.update();
        });
    }

    const btnRotate = document.getElementById('btn-toggle-rotate');
    if (btnRotate) {
        btnRotate.addEventListener('click', () => {
            autoRotate = !autoRotate;
            btnRotate.innerText = autoRotate ? 'Auto-Rotación: ON' : 'Auto-Rotación: OFF';
        });
    }

    animate();
}

function onWindowResize() {
    const container = document.getElementById('canvas-container');
    if (!container || !renderer || !camera) return;
    const w = container.clientWidth;
    const h = container.clientHeight;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
}

function animate() {
    requestAnimationFrame(animate);
    if (autoRotate) {
        if (neuronsGroup) neuronsGroup.rotation.y += 0.0025;
        if (synapsesGroup) synapsesGroup.rotation.y += 0.0025;
        if (physarumGroup) physarumGroup.rotation.y += 0.0025;
        if (myceliumGroup) myceliumGroup.rotation.y += 0.0025;
    }
    if (controls) controls.update();
    if (renderer && scene && camera) renderer.render(scene, camera);
}

// ============================================================================
let csiChart;

// ============================================================================
// CHART.JS TELEMETRÍA (CSI SPECTRUM & NEUROMODULACIÓN)
// ============================================================================
function initCharts() {
    Chart.defaults.color = '#8a93b5';
    Chart.defaults.font.family = "'Space Grotesk', sans-serif";

    // 1. Analizador de Espectro CSI (64 Subportadoras)
    const csiCanvas = document.getElementById('csiSpectrumChart');
    if (csiCanvas) {
        const csiCtx = csiCanvas.getContext('2d');
        const labels64 = Array.from({length: 64}, (_, i) => `SC${i}`);
        csiChart = new Chart(csiCtx, {
            type: 'line',
            data: {
                labels: labels64,
                datasets: [
                    {
                        label: 'Amplitud (H_dual)',
                        data: new Array(64).fill(0),
                        borderColor: '#00e5ff',
                        backgroundColor: 'rgba(0, 229, 255, 0.12)',
                        borderWidth: 1.8,
                        pointRadius: 0,
                        fill: true,
                        tension: 0.15
                    },
                    {
                        label: 'Fase Dif Δφ (rad)',
                        data: new Array(63).fill(0),
                        borderColor: '#ffa200',
                        backgroundColor: 'transparent',
                        borderWidth: 1.2,
                        pointRadius: 0,
                        fill: false,
                        tension: 0.15
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                plugins: {
                    legend: { position: 'top', labels: { boxWidth: 8, font: { size: 9 } } }
                },
                scales: {
                    x: { display: false },
                    y: { 
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                        ticks: { font: { size: 9 }, color: '#8a93b5' }
                    }
                }
            }
        });
    }

    // 2. Neuromoduladores
    const canvas = document.getElementById('neuroChart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    neuroChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Dopamina',
                    data: [],
                    borderColor: '#ffcc00',
                    backgroundColor: 'rgba(255, 204, 0, 0.08)',
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: true,
                    tension: 0.2
                },
                {
                    label: 'Serotonina',
                    data: [],
                    borderColor: '#b000ff',
                    backgroundColor: 'transparent',
                    borderWidth: 1.8,
                    pointRadius: 0,
                    fill: false,
                    tension: 0.2
                },
                {
                    label: 'Acetilcolina',
                    data: [],
                    borderColor: '#00e5ff',
                    backgroundColor: 'transparent',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    fill: false,
                    tension: 0.2
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            plugins: {
                legend: {
                    position: 'top',
                    labels: { boxWidth: 8, font: { size: 9 } }
                }
            },
            scales: {
                y: {
                    min: 0,
                    max: 1.0,
                    grid: { color: 'rgba(255,255,255,0.04)' },
                    ticks: { font: { size: 8 } }
                },
                x: {
                    grid: { display: false },
                    ticks: { display: false }
                }
            }
        }
    });
}

// ============================================================================
// ACTUALIZACIÓN DEL GRAFO 3D
// ============================================================================
function updateNetwork3D(data) {
    if (!data.neurons || !Array.isArray(data.neurons)) return;

    // 1. Inicialización de los meshes de las 274 neuronas
    if (!isInitialized) {
        while (neuronsGroup.children.length > 0) {
            neuronsGroup.remove(neuronsGroup.children[0]);
        }
        neuronMeshes = [];

        const sphereGeo = new THREE.SphereGeometry(1.8, 16, 16);

        for (let i = 0; i < data.neurons.length; i++) {
            const n = data.neurons[i];
            const layer = (n.layer_id !== undefined) ? n.layer_id : (n.layer || 0);
            const color = layerColors[layer] || 0x00e5ff;

            const mat = new THREE.MeshStandardMaterial({
                color: color,
                emissive: color,
                emissiveIntensity: 0.2,
                roughness: 0.35,
                metalness: 0.2
            });

            const mesh = new THREE.Mesh(sphereGeo, mat);
            // Si no vienen coordenadas 3D, posicionar cilíndricamente por capa
            let x = n.x, y = n.y, z = n.z;
            if (x === undefined || (x === 0 && y === 0 && z === 0)) {
                const angle = (i % 20) * (Math.PI * 2 / 20);
                const radius = 25 + (layer * 8);
                x = Math.cos(angle) * radius;
                z = Math.sin(angle) * radius;
                y = (layer - 3) * 18 + ((i % 5) - 2) * 3;
            }
            mesh.position.set(x, y, z);

            mesh.userData = {
                id: i,
                layer: layer,
                baseColor: new THREE.Color(color)
            };

            neuronsGroup.add(mesh);
            neuronMeshes.push(mesh);
        }
        isInitialized = true;
    }

    // 2. Dinámica de brillo según firing_rate y actividad
    for (let i = 0; i < neuronMeshes.length && i < data.neurons.length; i++) {
        const mesh = neuronMeshes[i];
        const n = data.neurons[i];
        const fr = n.firing_rate || n.firing || 0;

        if (fr > 5.0) {
            // Neurona en descarga activa: aumento de escala y destello
            mesh.scale.set(1.6, 1.6, 1.6);
            mesh.material.emissive.setHex(0xffffff);
            mesh.material.emissiveIntensity = Math.min(1.2, 0.4 + (fr / 50.0));
        } else {
            // Reposo con brillo proporcional a energía
            mesh.scale.set(1.0, 1.0, 1.0);
            mesh.material.emissive.copy(mesh.userData.baseColor);
            const energy = n.energy !== undefined ? n.energy : 1.0;
            mesh.material.emissiveIntensity = 0.15 + 0.25 * energy;
        }
    }

    // 3. Renderizado de arcos sinápticos
    if (data.synapses && Array.isArray(data.synapses)) {
        while (synapsesGroup.children.length > 0) {
            const child = synapsesGroup.children[0];
            if (child.geometry) child.geometry.dispose();
            if (child.material) child.material.dispose();
            synapsesGroup.remove(child);
        }

        const synapses = data.synapses;
        for (let s = 0; s < synapses.length; s++) {
            const syn = synapses[s];
            if (syn.w > 0.2) {
                const preMesh = neuronMeshes[syn.pre];
                const postMesh = neuronMeshes[syn.post];
                if (preMesh && postMesh) {
                    const p1 = preMesh.position;
                    const p2 = postMesh.position;

                    let lineColor = syn.exc 
                        ? new THREE.Color(layerColors[preMesh.userData.layer] || 0x00e5ff)
                        : new THREE.Color(0xff3366);

                    const lineGeo = new THREE.BufferGeometry().setFromPoints([p1, p2]);
                    const lineMat = new THREE.LineBasicMaterial({
                        color: lineColor,
                        transparent: true,
                        opacity: Math.min(syn.w * 0.35, 0.6),
                        linewidth: 1
                    });

                    const line = new THREE.Line(lineGeo, lineMat);
                    synapsesGroup.add(line);
                }
            }
        }
    }

    // 4. Renderizado de túbulos protoplásmicos de Physarum (Moho Mucilaginoso)
    if (data.physarum && data.physarum.tubules && Array.isArray(data.physarum.tubules)) {
        while (physarumGroup.children.length > 0) {
            const child = physarumGroup.children[0];
            if (child.geometry) child.geometry.dispose();
            if (child.material) child.material.dispose();
            physarumGroup.remove(child);
        }

        const tubules = data.physarum.tubules;
        for (let t = 0; t < tubules.length; t++) {
            const tub = tubules[t];
            const meshA = neuronMeshes[tub.a];
            const meshB = neuronMeshes[tub.b];
            if (meshA && meshB && tub.d > 0.3) {
                const p1 = meshA.position;
                const p2 = meshB.position;
                const lineGeo = new THREE.BufferGeometry().setFromPoints([p1, p2]);
                const lineMat = new THREE.LineBasicMaterial({
                    color: 0xffea00, // Oro protoplásmico brillante
                    transparent: true,
                    opacity: Math.min(0.85, tub.d * 0.4),
                    linewidth: Math.max(1, Math.round(tub.d))
                });
                const line = new THREE.Line(lineGeo, lineMat);
                physarumGroup.add(line);
            }
        }
    }

    // 4. Dibujar sustrato micelial fúngico (hifas bioluminiscentes verde esmeralda)
    if (data.mycelium && data.mycelium.nodes && data.mycelium.cords && Array.isArray(data.mycelium.cords)) {
        while (myceliumGroup.children.length > 0) {
            const child = myceliumGroup.children[0];
            if (child.geometry) child.geometry.dispose();
            if (child.material) child.material.dispose();
            myceliumGroup.remove(child);
        }

        const nodes = data.mycelium.nodes;
        for (const cord of data.mycelium.cords) {
            const na = nodes[cord.a];
            const nb = nodes[cord.b];
            if (na && nb) {
                const p1 = new THREE.Vector3(na.x, na.y, na.z);
                const p2 = new THREE.Vector3(nb.x, nb.y, nb.z);
                const lineGeo = new THREE.BufferGeometry().setFromPoints([p1, p2]);
                const lineMat = new THREE.LineBasicMaterial({
                    color: 0x00ffaa,
                    transparent: true,
                    opacity: Math.min(0.85, cord.m * 0.5)
                });
                const line = new THREE.Line(lineGeo, lineMat);
                myceliumGroup.add(line);
            }
        }
    }
}

// ============================================================================
// ACTUALIZACIÓN DE TELEMETRÍA Y DASHBOARD
// ============================================================================
function updateDashboard(data) {
    // 1. Hardware y Nodos RF
    const rx1Pkts = data.rx1_pkts !== undefined ? data.rx1_pkts : 0;
    const rx2Pkts = data.rx2_pkts !== undefined ? data.rx2_pkts : 0;
    const rx1Rssi = data.rx1_rssi !== undefined ? data.rx1_rssi : -80;
    const rx2Rssi = data.rx2_rssi !== undefined ? data.rx2_rssi : -60;

    const elRx1Pkts = document.getElementById('rx1-pkts');
    if (elRx1Pkts) elRx1Pkts.innerText = rx1Pkts.toLocaleString();
    const elRx2Pkts = document.getElementById('rx2-pkts');
    if (elRx2Pkts) elRx2Pkts.innerText = rx2Pkts.toLocaleString();

    const elRx1Rssi = document.getElementById('rx1-rssi');
    if (elRx1Rssi) elRx1Rssi.innerText = rx1Rssi + ' dBm';
    const elRx2Rssi = document.getElementById('rx2-rssi');
    if (elRx2Rssi) elRx2Rssi.innerText = rx2Rssi + ' dBm';

    // Barra de señal RSSI (-100dBm = 0%, -30dBm = 100%)
    const pct1 = Math.max(5, Math.min(100, Math.round((rx1Rssi + 100) * (100 / 70))));
    const pct2 = Math.max(5, Math.min(100, Math.round((rx2Rssi + 100) * (100 / 70))));
    const bar1 = document.getElementById('rx1-rssi-bar');
    if (bar1) bar1.style.width = pct1 + '%';
    const bar2 = document.getElementById('rx2-rssi-bar');
    if (bar2) bar2.style.width = pct2 + '%';

    // Badge de estado de hardware
    const hwText = document.getElementById('hw-status-text');
    if (hwText) {
        if (data.hardware_connected) {
            hwText.innerText = 'ESP32 DUAL CONECTADO';
        } else {
            hwText.innerText = 'MODO SIMULACIÓN';
        }
    }

    // === 1.1 TELEMETRÍA RF & SNR ===
    const snrDb = data.snr_db !== undefined ? data.snr_db : 20.0;
    const elSnr = document.getElementById('val-snr');
    if (elSnr) elSnr.innerText = snrDb.toFixed(1) + ' dB';

    // Actualizar Espectro CSI en vivo (64 Subportadoras)
    if (csiChart && data.csi_amp && Array.isArray(data.csi_amp)) {
        csiChart.data.datasets[0].data = data.csi_amp;
        if (data.csi_phase && Array.isArray(data.csi_phase)) {
            csiChart.data.datasets[1].data = data.csi_phase;
        }
        csiChart.update('none'); // Update ultra-rápido sin animación
    }

    // 2. Estado Cerebral (AWAKE / SWS / REM)
    const brainState = data.brain_state || data.state || 'AWAKE';
    const stateBadge = document.getElementById('sim-state-badge');
    if (stateBadge) {
        stateBadge.innerText = brainState;
        stateBadge.className = 'state-badge';
        if (brainState === 'AWAKE') stateBadge.classList.add('state-awake');
        else if (brainState === 'SLOW_WAVE_SLEEP') stateBadge.classList.add('state-sws');
        else if (brainState === 'REM') stateBadge.classList.add('state-rem');
    }

    const elStep = document.getElementById('val-step');
    if (elStep) elStep.innerText = (data.step_count || 0).toLocaleString();
    const elTime = document.getElementById('val-time');
    if (elTime) elTime.innerText = `${(data.time_ms || 0) / 1000}s`;

    // 3. Cinemática Polar & Radar AoA
    const sp = data.spatial_target || {};
    const elAngle = document.getElementById('val-aoa-angle');
    if (elAngle) elAngle.innerText = `${(sp.angle_deg || 0).toFixed(1)}°`;
    const elDist = document.getElementById('val-aoa-dist');
    if (elDist) elDist.innerText = `${(sp.distance_m || 0).toFixed(2)} m`;
    const elCoords = document.getElementById('val-aoa-coords');
    if (elCoords) {
        const x = sp.x_m !== undefined ? sp.x_m.toFixed(2) : '0.00';
        const y = sp.y_m !== undefined ? sp.y_m.toFixed(2) : '0.00';
        elCoords.innerText = `X: ${x}m, Y: ${y}m`;
    }
    const elVel = document.getElementById('val-aoa-vel');
    if (elVel) elVel.innerText = `${(sp.velocity_mps || 0).toFixed(2)} m/s`;

    const elDopC = document.getElementById('val-doppler-c');
    if (elDopC) elDopC.innerText = `${(data.doppler_centroid_hz || 0).toFixed(1)} Hz`;
    const elDopS = document.getElementById('val-doppler-s');
    if (elDopS) elDopS.innerText = `${(data.doppler_spread_hz || 0).toFixed(1)} Hz`;

    // === 4. DECISIÓN & BIOMETRÍA (PHYSARUM & MYCELIUM) ===
    const ph = data.physarum || {};
    const elPhTarget = document.getElementById('val-physarum-target');
    if (elPhTarget) elPhTarget.innerText = ph.winning_target || 'VACIO';
    const elPhConf = document.getElementById('val-physarum-conf');
    if (elPhConf) elPhConf.innerText = `${Math.round((ph.confidence || 0) * 100)}%`;
    const elPhTub = document.getElementById('val-physarum-tubules');
    if (elPhTub) elPhTub.innerText = `${ph.active_tubules || 0} / ${Math.round(ph.digested_waste || 0)}`;

    const gSim = data.siamese_genuine !== undefined ? data.siamese_genuine : 0.0;
    const elG = document.getElementById('val-genuine');
    if (elG) elG.innerText = gSim.toFixed(3);

    // Sustrato Micelial
    if (data.mycelium) {
        const dwellSec = data.mycelium.dwell_time_sec || 0;
        const dMin = Math.floor(dwellSec / 60);
        const dSec = Math.floor(dwellSec % 60);
        const driftDb = (data.mycelium.drift_compensation_db || 0).toFixed(2);
        const elMycInfo = document.getElementById('val-myc-info');
        if (elMycInfo) elMycInfo.innerText = `${driftDb} dB | ${dMin}m ${dSec}s`;
    }

    // === 5. TABLA DIAGNÓSTICA DE POBLACIÓN NEUROMÓRFICA ===
    const tbody = document.getElementById('layer-table-body');
    if (tbody && data.layer_diagnostics && Array.isArray(data.layer_diagnostics)) {
        let html = '';
        for (const layer of data.layer_diagnostics) {
            let tagClass = 'tag-optimal';
            let tagText = 'ÓPTIMA';
            if (layer.sat_pct > 30.0) {
                tagClass = 'tag-saturated';
                tagText = 'SATURADA';
            } else if (layer.silent_pct > 50.0) {
                tagClass = 'tag-silent';
                tagText = 'SILENCIOSA';
            }

            html += `<tr>
                <td><strong style="color: ${layerColors[layer.layer_id] ? '#' + layerColors[layer.layer_id].toString(16).padStart(6, '0') : '#fff'}">${layer.name}</strong></td>
                <td>${layer.neurons} N</td>
                <td style="font-weight: 700; color: #00e5ff;">${layer.mean_fr.toFixed(1)}</td>
                <td>${layer.max_fr.toFixed(0)}</td>
                <td>${layer.mean_v.toFixed(1)} mV</td>
                <td>${layer.silent_pct.toFixed(0)}%</td>
                <td>${layer.sat_pct.toFixed(0)}%</td>
                <td><span class="layer-tag ${tagClass}">${tagText}</span></td>
            </tr>`;
        }
        tbody.innerHTML = html;
    }

    const timeLabel = timeSec.toFixed(1) + 's';
    historyData.time.push(timeLabel);
    historyData.da.push(da);
    historyData.ser.push(ser);
    historyData.ach.push(ach);

    if (historyData.time.length > maxHistoryLen) {
        historyData.time.shift();
        historyData.da.shift();
        historyData.ser.shift();
        historyData.ach.shift();
    }

    if (neuroChart) {
        neuroChart.data.labels = historyData.time;
        neuroChart.data.datasets[0].data = historyData.da;
        neuroChart.data.datasets[1].data = historyData.ser;
        neuroChart.data.datasets[2].data = historyData.ach;
        neuroChart.update('none');
    }
}

// ============================================================================
// POLLING CONTINUO ASÍNCRONO
// ============================================================================
async function fetchState() {
    try {
        const res = await fetch('/sim_state.json?t=' + Date.now());
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        updateNetwork3D(data);
        updateDashboard(data);
    } catch (err) {
        // En caso de que el servidor esté compilando o reiniciando
        console.warn('Conectando con Cerebro C++...', err.message);
    }
}

// Inicialización en DOM Ready
document.addEventListener('DOMContentLoaded', () => {
    init3D();
    initCharts();
    // Polling fluido a 150 ms
    setInterval(fetchState, 150);
});