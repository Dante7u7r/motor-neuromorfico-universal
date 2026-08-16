/**
 * CEREBRO • DASHBOARD DE DEMOSTRACIÓN & DEFENSA (demo.js)
 * Visualización Biomédica, Clasificación en Vivo y Radar de Sala 2D/3D
 */

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
let scene, camera, renderer, controls;
let neuronsGroup, synapsesGroup, physarumGroup;
let neuronMeshes = [];
let ecgChart, respChart;
let radarCanvas, radarCtx;
let radarScanAngle = 0;

// Paleta de colores por capa
const layerColors = {
    0: 0x00e5ff, // Sensorial CSI
    1: 0x4466ff, // Oculta
    4: 0xffa200, // Tálamo
    3: 0xb000ff, // PFC
    2: 0xff3366, // Motora
    5: 0x00ff88, // Entorrinal
    6: 0xffea00  // Hipocampo
};

// ============================================================================
// INICIALIZACIÓN
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
    init3D();
    initVitalCharts();
    initRadarCanvas();
    startPolling();
});

// ============================================================================
// 1. MOTOR 3D (THREE.JS)
// ============================================================================
function init3D() {
    const container = document.getElementById('demo-canvas-container');
    if (!container) return;

    const width = container.clientWidth || 600;
    const height = container.clientHeight || 380;

    scene = new THREE.Scene();
    scene.fog = new THREE.FogExp2(0x070814, 0.0035);

    camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 1000);
    camera.position.set(0, 35, 150);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.maxDistance = 300;
    controls.minDistance = 20;

    const ambientLight = new THREE.AmbientLight(0xffffff, 0.85);
    scene.add(ambientLight);

    const dirLight = new THREE.DirectionalLight(0x00e5ff, 1.2);
    dirLight.position.set(50, 100, 50);
    scene.add(dirLight);

    neuronsGroup = new THREE.Group();
    synapsesGroup = new THREE.Group();
    physarumGroup = new THREE.Group();
    myceliumGroup = new THREE.Group();
    scene.add(neuronsGroup);
    scene.add(synapsesGroup);
    scene.add(physarumGroup);
    scene.add(myceliumGroup);

    window.addEventListener('resize', onWindowResize);

    const btnReset = document.getElementById('btn-demo-reset');
    if (btnReset) {
        btnReset.addEventListener('click', () => {
            camera.position.set(0, 35, 150);
            controls.target.set(0, 0, 0);
            controls.update();
        });
    }

    animate();
}

function onWindowResize() {
    const container = document.getElementById('demo-canvas-container');
    if (!container || !renderer || !camera) return;
    const w = container.clientWidth;
    const h = container.clientHeight;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
}

function animate() {
    requestAnimationFrame(animate);
    if (neuronsGroup) neuronsGroup.rotation.y += 0.002;
    if (synapsesGroup) synapsesGroup.rotation.y += 0.002;
    if (physarumGroup) physarumGroup.rotation.y += 0.002;
    if (myceliumGroup) myceliumGroup.rotation.y += 0.002;
    if (controls) controls.update();
    if (renderer && scene && camera) renderer.render(scene, camera);
}

// ============================================================================
// 2. MONITORES DE FORMAS DE ONDA (CHARTS ECG / RESPIRACIÓN)
// ============================================================================
function initVitalCharts() {
    Chart.defaults.color = '#8b95b5';
    Chart.defaults.font.family = "'JetBrains Mono', monospace";

    // ECG / Pulso
    const ecgCanvas = document.getElementById('ecgWaveChart');
    if (ecgCanvas) {
        const ctx = ecgCanvas.getContext('2d');
        ecgChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array.from({length: 60}, (_, i) => i),
                datasets: [{
                    data: new Array(60).fill(0),
                    borderColor: '#ff2a6d',
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: false,
                    tension: 0.25
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                plugins: { legend: { display: false } },
                scales: {
                    x: { display: false },
                    y: { 
                        display: false,
                        min: -0.8,
                        max: 1.5
                    }
                }
            }
        });
    }

    // Respiración
    const respCanvas = document.getElementById('respWaveChart');
    if (respCanvas) {
        const ctx = respCanvas.getContext('2d');
        respChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array.from({length: 60}, (_, i) => i),
                datasets: [{
                    data: new Array(60).fill(0),
                    borderColor: '#00e5ff',
                    backgroundColor: 'rgba(0, 229, 255, 0.08)',
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                plugins: { legend: { display: false } },
                scales: {
                    x: { display: false },
                    y: { 
                        display: false,
                        min: -1.2,
                        max: 1.2
                    }
                }
            }
        });
    }
}

// ============================================================================
// 3. RADAR DE SALA 2D (CANVAS INTERACTIVO)
// ============================================================================
function initRadarCanvas() {
    radarCanvas = document.getElementById('radarRoomCanvas');
    if (!radarCanvas) return;
    radarCtx = radarCanvas.getContext('2d');
}

function drawRadar2D(targetX, targetY, angleDeg, distM, isPresent) {
    if (!radarCtx || !radarCanvas) return;
    const w = radarCanvas.width;
    const h = radarCanvas.height;
    const originX = w / 2;
    const originY = h - 20;

    radarCtx.clearRect(0, 0, w, h);

    // Fondo y Anillos de Distancia (1m, 2m, 3m, 4m)
    const maxDist = 4.0; // 4 metros máximo
    const scale = (h - 40) / maxDist;

    radarCtx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
    radarCtx.lineWidth = 1;

    for (let r = 1; r <= 4; r++) {
        radarCtx.beginPath();
        radarCtx.arc(originX, originY, r * scale, Math.PI, 2 * Math.PI, false);
        radarCtx.stroke();

        radarCtx.fillStyle = 'rgba(255, 255, 255, 0.3)';
        radarCtx.font = '9px "JetBrains Mono"';
        radarCtx.fillText(`${r}m`, originX + 5, originY - r * scale + 10);
    }

    // Líneas Guía Radiales (-45°, 0°, +45°)
    const angles = [-Math.PI / 4, 0, Math.PI / 4];
    for (const a of angles) {
        const rad = -Math.PI / 2 + a;
        radarCtx.beginPath();
        radarCtx.moveTo(originX, originY);
        radarCtx.lineTo(originX + Math.cos(rad) * (maxDist * scale), originY + Math.sin(rad) * (maxDist * scale));
        radarCtx.stroke();
    }

    // Línea de barrido de radar animada
    radarScanAngle += 0.035;
    const scanDir = Math.sin(radarScanAngle) * (Math.PI / 3);
    const scanRad = -Math.PI / 2 + scanDir;

    radarCtx.strokeStyle = 'rgba(0, 229, 255, 0.4)';
    radarCtx.lineWidth = 1.5;
    radarCtx.beginPath();
    radarCtx.moveTo(originX, originY);
    radarCtx.lineTo(originX + Math.cos(scanRad) * (maxDist * scale), originY + Math.sin(scanRad) * (maxDist * scale));
    radarCtx.stroke();

    // Origen de Antenas ESP32
    radarCtx.fillStyle = '#00ff88';
    radarCtx.beginPath();
    radarCtx.arc(originX, originY, 4, 0, 2 * Math.PI);
    radarCtx.fill();
    radarCtx.fillText('ESP32 RX', originX - 22, originY + 14);

    // Objetivo Detectado
    if (isPresent && distM > 0.1) {
        const targetCanvasX = originX + targetX * scale;
        const targetCanvasY = originY - targetY * scale;

        // Halo pulsante
        const pulseR = 10 + Math.sin(Date.now() * 0.008) * 4;
        radarCtx.fillStyle = 'rgba(0, 229, 255, 0.25)';
        radarCtx.beginPath();
        radarCtx.arc(targetCanvasX, targetCanvasY, pulseR, 0, 2 * Math.PI);
        radarCtx.fill();

        // Punto central del objetivo
        radarCtx.fillStyle = '#00e5ff';
        radarCtx.beginPath();
        radarCtx.arc(targetCanvasX, targetCanvasY, 5, 0, 2 * Math.PI);
        radarCtx.fill();

        // Etiqueta del objetivo
        radarCtx.fillStyle = '#ffffff';
        radarCtx.font = '10px "Outfit"';
        radarCtx.fillText(`Sujeto (${distM.toFixed(1)}m, ${angleDeg >= 0 ? '+' : ''}${angleDeg.toFixed(0)}°)`, targetCanvasX + 8, targetCanvasY - 5);
    }
}

// ============================================================================
// 4. BUCLE DE TELEMETRÍA Y ACTUALIZACIÓN EN VIVO
// ============================================================================
function startPolling() {
    setInterval(() => {
        fetch('/sim_state.json?t=' + Date.now())
            .then(res => res.json())
            .then(data => {
                updateDemoUI(data);
                render3DBrain(data);
            })
            .catch(err => {
                console.warn("Reintentando conexión con Cerebro C++...", err);
            });
    }, 90);
}

function updateDemoUI(data) {
    const sp = data.spatial_target || {};
    const vs = data.vital_signs || {};
    const ph = data.physarum || {};
    const isPresent = sp.present || false;
    const winningTarget = ph.winning_target || 'VACIO';
    const confidence = Math.round((ph.confidence || sp.confidence || 0.85) * 100);

    // 1. Tarjeta Hero de Clasificación
    const heroCard = document.getElementById('card-hero-status');
    const heroIcon = document.getElementById('hero-icon');
    const heroLabel = document.getElementById('hero-label');
    const heroSublabel = document.getElementById('hero-sublabel');
    const heroConfText = document.getElementById('hero-confidence-text');
    const heroConfBar = document.getElementById('hero-confidence-bar');
    const heroActBadge = document.getElementById('hero-activity-badge');

    if (heroConfText) heroConfText.innerText = `${confidence}%`;
    if (heroConfBar) heroConfBar.style.width = `${confidence}%`;
    if (heroActBadge) heroActBadge.innerText = vs.activity || 'REPOSO / RESPIRACIÓN';

    if (!isPresent || winningTarget === 'VACIO') {
        if (heroIcon) heroIcon.innerText = '🚪';
        if (heroLabel) heroLabel.innerText = 'HABITACIÓN VACÍA';
        if (heroSublabel) heroSublabel.innerText = 'Línea base térmica y estática registrada';
        if (heroActBadge) heroActBadge.innerText = 'REPOSO TÉRMICO';
    } else if (winningTarget === 'MASCOTA') {
        if (heroIcon) heroIcon.innerText = '🐾';
        if (heroLabel) heroLabel.innerText = 'MASCOTA DETECTADA';
        if (heroSublabel) heroSublabel.innerText = 'Firma cinemática de cuadrúpedo / micro-Doppler';
    } else if (winningTarget === 'HUMANO_B') {
        if (heroIcon) heroIcon.innerText = '👤';
        if (heroLabel) heroLabel.innerText = 'HUMANO (VISITANTE)';
        if (heroSublabel) heroSublabel.innerText = 'Firma de absorción no catalogada previamente';
    } else if (winningTarget === 'MULTITUD') {
        if (heroIcon) heroIcon.innerText = '👥';
        if (heroLabel) heroLabel.innerText = 'MULTITUD DETECTADA';
        if (heroSublabel) heroSublabel.innerText = 'Múltiples centros de dispersión y alta entropía';
    } else {
        // HUMANO_A / Default
        if (heroIcon) heroIcon.innerText = '👤';
        if (heroLabel) heroLabel.innerText = 'HUMANO DETECTADO';
        if (heroSublabel) heroSublabel.innerText = 'Sujeto Autorizado (Perfil Genuino Reconocido)';
    }

    // 2. Biometría Siamesa STDP
    const gSim = data.siamese_genuine !== undefined ? data.siamese_genuine : 0.65;
    const elBioVal = document.getElementById('bio-genuine-val');
    if (elBioVal) elBioVal.innerText = gSim.toFixed(3);
    const elBioBar = document.getElementById('bio-genuine-bar');
    if (elBioBar) elBioBar.style.width = Math.min(100, Math.round(gSim * 100)) + '%';

    // 3. Signos Vitales
    const elBpm = document.getElementById('vital-bpm');
    const elRpm = document.getElementById('vital-rpm');

    if (!isPresent) {
        if (elBpm) elBpm.innerText = '--';
        if (elRpm) elRpm.innerText = '--';
    } else {
        if (elBpm) elBpm.innerText = Math.round(vs.bpm || 72);
        if (elRpm) elRpm.innerText = Math.round(vs.rpm || 16);
    }

    // Actualizar ondas
    if (ecgChart && vs.waveform_ecg && Array.isArray(vs.waveform_ecg)) {
        ecgChart.data.datasets[0].data = vs.waveform_ecg;
        ecgChart.update('none');
    }
    if (respChart && vs.waveform_resp && Array.isArray(vs.waveform_resp)) {
        respChart.data.datasets[0].data = vs.waveform_resp;
        respChart.update('none');
    }

    // 4. Radar 2D de Sala
    const angleDeg = sp.angle_deg || 0.0;
    const distM = sp.distance_m || 0.0;
    const xM = sp.x_m || 0.0;
    const yM = sp.y_m || 0.0;
    const velMps = sp.velocity_mps || 0.0;

    const elAngle = document.getElementById('radar-angle');
    if (elAngle) elAngle.innerText = `${angleDeg >= 0 ? '+' : ''}${angleDeg.toFixed(1)}°`;
    const elDist = document.getElementById('radar-dist');
    if (elDist) elDist.innerText = `${distM.toFixed(2)} m`;
    const elCoords = document.getElementById('radar-coords');
    if (elCoords) elCoords.innerText = `X: ${xM.toFixed(2)}m, Y: ${yM.toFixed(2)}m`;
    const elVel = document.getElementById('radar-vel');
    if (elVel) elVel.innerText = `${velMps.toFixed(2)} m/s`;

    // 5. Sustrato Micelial & Anti-Drift
    if (data.mycelium) {
        const dwellSec = data.mycelium.dwell_time_sec || 0;
        const dMin = Math.floor(dwellSec / 60);
        const dSec = Math.floor(dwellSec % 60);
        const elDwell = document.getElementById('myc-dwell');
        if (elDwell) elDwell.innerText = `${dMin}m ${dSec}s`;

        const driftDb = data.mycelium.drift_compensation_db || 0;
        const elDrift = document.getElementById('myc-drift');
        if (elDrift) elDrift.innerText = `${driftDb.toFixed(2)} dB`;
    }

    // 6. Sustratos Bio-Híbridos (Plantae & Fungi Quorum)
    if (data.fungal_quorum) {
        const occ = data.fungal_quorum.occupants || 0;
        const qLabel = data.fungal_quorum.label || 'SALA VACIA';
        const elOcc = document.getElementById('bio-quorum-occupants');
        if (elOcc) elOcc.innerText = occ === 0 ? '0 Personas (Vacío)' : (occ === 1 ? '1 Persona (Usuario)' : `${occ} Personas`);
        const elBadge = document.getElementById('quorum-label-badge');
        if (elBadge) {
            elBadge.innerText = occ === 0 ? 'VACÍO' : `${occ} ${occ === 1 ? 'PERSONA' : 'PERSONAS'}`;
            elBadge.style.color = occ === 0 ? '#8b95b5' : '#00e5ff';
            elBadge.style.background = occ === 0 ? 'rgba(139,149,181,0.15)' : 'rgba(0,229,255,0.15)';
        }
    }

    if (data.auxin_beamformer) {
        const steerDeg = data.auxin_beamformer.steered_angle_deg || 0;
        const snrGain = data.auxin_beamformer.snr_gain_db || 0;
        const elAuxin = document.getElementById('bio-auxin-steer');
        if (elAuxin) elAuxin.innerText = `${steerDeg >= 0 ? '+' : ''}${steerDeg.toFixed(1)}° (+${snrGain.toFixed(1)}dB)`;
    }

    drawRadar2D(xM, yM, angleDeg, distM, isPresent);
}

// ============================================================================
// 5. RENDERIZADO 3D DE LA RED NEURONAL
// ============================================================================
function render3DBrain(data) {
    if (!neuronsGroup || !data.neurons) return;

    // Crear esferas de neuronas si es la primera vez
    if (neuronMeshes.length === 0) {
        const geo = new THREE.SphereGeometry(1.4, 16, 16);
        for (let i = 0; i < data.neurons.length; i++) {
            const n = data.neurons[i];
            const col = layerColors[n.layer] || 0xffffff;
            const mat = new THREE.MeshPhongMaterial({
                color: col,
                emissive: col,
                emissiveIntensity: 0.35,
                shininess: 90
            });
            const mesh = new THREE.Mesh(geo, mat);
            mesh.position.set(n.x, n.y, n.z);
            neuronsGroup.add(mesh);
            neuronMeshes.push(mesh);
        }
    }

    // Actualizar brillo y escala según tasa de disparo
    for (let i = 0; i < data.neurons.length && i < neuronMeshes.length; i++) {
        const n = data.neurons[i];
        const mesh = neuronMeshes[i];
        const fr = n.firing_rate || n.firing || 0;

        if (fr > 10.0) {
            mesh.scale.set(1.4, 1.4, 1.4);
            mesh.material.emissiveIntensity = 0.9;
        } else {
            mesh.scale.set(1.0, 1.0, 1.0);
            mesh.material.emissiveIntensity = 0.35;
        }
    }

    // Dibujar sinapsis
    if (data.synapses && Array.isArray(data.synapses)) {
        while (synapsesGroup.children.length > 0) {
            const child = synapsesGroup.children[0];
            if (child.geometry) child.geometry.dispose();
            if (child.material) child.material.dispose();
            synapsesGroup.remove(child);
        }

        const maxDraw = Math.min(data.synapses.length, 120);
        for (let s = 0; s < maxDraw; s++) {
            const syn = data.synapses[s];
            const meshA = neuronMeshes[syn.pre];
            const meshB = neuronMeshes[syn.post];
            if (meshA && meshB && syn.w > 0.4) {
                const p1 = meshA.position;
                const p2 = meshB.position;
                const lineGeo = new THREE.BufferGeometry().setFromPoints([p1, p2]);
                const lineMat = new THREE.LineBasicMaterial({
                    color: syn.exc ? 0x00e5ff : 0xff2a6d,
                    transparent: true,
                    opacity: Math.min(0.6, syn.w * 0.4)
                });
                const line = new THREE.Line(lineGeo, lineMat);
                synapsesGroup.add(line);
            }
        }
    }

    // Dibujar túbulos dorados de Physarum
    if (data.physarum && data.physarum.tubules && Array.isArray(data.physarum.tubules)) {
        while (physarumGroup.children.length > 0) {
            const child = physarumGroup.children[0];
            if (child.geometry) child.geometry.dispose();
            if (child.material) child.material.dispose();
            physarumGroup.remove(child);
        }

        for (const tub of data.physarum.tubules) {
            const meshA = neuronMeshes[tub.a];
            const meshB = neuronMeshes[tub.b];
            if (meshA && meshB && tub.d > 0.3) {
                const lineGeo = new THREE.BufferGeometry().setFromPoints([meshA.position, meshB.position]);
                const lineMat = new THREE.LineBasicMaterial({
                    color: 0xffea00,
                    transparent: true,
                    opacity: Math.min(0.9, tub.d * 0.45)
                });
                const line = new THREE.Line(lineGeo, lineMat);
                physarumGroup.add(line);
            }
        }
    }

    // Dibujar sustrato micelial fúngico (hifas bioluminiscentes verde esmeralda)
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
