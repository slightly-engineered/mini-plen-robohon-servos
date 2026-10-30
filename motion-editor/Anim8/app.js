console.log("APP.JS STARTED (ROBOHON 3-BUS + SERVO-ANGLE RIG VERSION)");

/* =========================================================
   IMPORTS (ES MODULES)
   ========================================================= */

import {
    Scene,
    PerspectiveCamera,
    WebGLRenderer,
    Color,
    Vector2,
    Vector3,
    Group,
    HemisphereLight,
    DirectionalLight,
    AmbientLight,
    PlaneGeometry,
    MeshPhongMaterial,
    Mesh,
    Box3,
    Raycaster,
    DoubleSide,
    GridHelper
} from './js/three.module.js';

import { OrbitControls } from './js/OrbitControls.js';
import { TransformControls } from './js/TransformControls.js';
import { GLTFLoader } from './js/GLTFLoader.js';
import { RobohonController } from './js/RobohonController.js';
import { initCalibrationPanel } from './js/calibrationPanel.js';
import { loadCalibration } from './core/robohonMap.js';

import { RigController } from './core/rigController.js';
import { Timeline } from './core/timeline.js';
import { ServoRig } from './core/servoRig.js';
import { Exporter } from './core/exporter.js';
import { Joints } from './core/joints.js';

/* =========================================================
   GLOBALS
   ========================================================= */

let scene, camera, renderer;
let orbitControls, transformControls;
let raycaster = new Raycaster();
let mouse = new Vector2();
let root;

const gltfLoader = new GLTFLoader();
let plen = null;
let rig = null;
let model = null;

let frameTimelineEl = null;

// Rate-limit hardware sends during playback (~33 fps; each send = 1 frame per active bus).
let _lastSendTime = 0;
const SEND_INTERVAL_MS = 30;

// Motion properties — persists across export calls.
const motionProps = {
    slot: 0,
    name: "New Motion",
    loop: { enabled: false, begin: 0, end: 0, count: 255 },
    jump: { enabled: false, begin: 0, end: 0, slot: 0  }
};

/* =========================================================
   STARTUP
   ========================================================= */

init();
animate();

/* =========================================================
   INIT
   ========================================================= */

function init() {

    frameTimelineEl = document.getElementById("frame-timeline");

    /* --- SCENE --- */
    scene = new Scene();
    scene.background = new Color(0x0d0f12);

    /* --- CAMERA --- */
    camera = new PerspectiveCamera(
        45,
        window.innerWidth / window.innerHeight,
        0.1,
        2000
    );
    window.camera = camera;
    camera.position.set(200, 200, 300);

    /* --- RENDERER --- */
    renderer = new WebGLRenderer({ antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.shadowMap.enabled = true;
    document.body.appendChild(renderer.domElement);

    /* --- ORBIT CONTROLS --- */
    orbitControls = new OrbitControls(camera, renderer.domElement);
    orbitControls.enableDamping = true;
    orbitControls.dampingFactor = 0.08;

    /* --- TRANSFORM CONTROLS --- */
    transformControls = new TransformControls(camera, renderer.domElement);
    transformControls.setMode("rotate");
    transformControls.setSpace("local");
    scene.add(transformControls);

    transformControls.addEventListener("dragging-changed", (event) => {
        orbitControls.enabled = !event.value;
    });

    /* =========================================================
       LIVE ROBOT SYNC + FRAME WRITEBACK (DURING DRAG)
       ========================================================= */
    transformControls.addEventListener("objectChange", () => {
        if (!rig) return;
        if (!transformControls.dragging) return;

        const joint = transformControls.object;
        if (!joint) return;

        const servoName = joint.name;
        if (!ServoRig[servoName]) return;

        const clampedAngle = rig.updateFromObject3D(servoName, joint);

        // Write updated pose back into the current frame in place.
        const frame = Timeline.frames[Timeline.currentFrame];
        if (frame) {
            frame.angles = rig.getAngleMap();
        }

        // Joints with no RoBoHoN servo assigned (legs) are ignored by the controller.
        if (plen && plen.isTransportReady()) {
            plen.sendSingle(servoName, clampedAngle);
        }
    });

    // Refresh thumbnail when drag ends.
    transformControls.addEventListener("mouseUp", () => {
        if (!rig) return;
        const idx = Timeline.currentFrame;
        if (Timeline.frames[idx]) {
            captureFrameThumbnail(idx);
            renderFrameTimeline();
        }
    });

    /* --- MOUSE PICKING --- */
    renderer.domElement.addEventListener("pointerdown", onPointerDown);

    /* --- LIGHTS --- */
    scene.add(new HemisphereLight(0xffffff, 0x444444, 1.0));

    const dir = new DirectionalLight(0xffffff, 1.0);
    dir.position.set(100, 200, 100);
    scene.add(dir);

    const ambient = new AmbientLight(0xffffff, 0.4);
    scene.add(ambient);

    /* =========================================================
       GRID + FLOOR
       ========================================================= */

    const grid = new GridHelper(1000, 4, 0x00ffff, 0x0088ff);
    grid.material.opacity = 0.9;
    grid.material.transparent = true;
    scene.add(grid);

    const floor = new Mesh(
        new PlaneGeometry(1000, 1000),
        new MeshPhongMaterial({ color: 0x050505, shininess: 40 })
    );
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    scene.add(floor);

    /* --- ROOT GROUP --- */
    root = new Group();
    scene.add(root);

    /* --- LOAD ROBOT --- */
    loadRobot();

    /* --- ROBOHON CONTROLLER (USB + BLE + WiFi) --- */
    loadCalibration();
    plen = new RobohonController();
    window.controller = plen;
    window.usb = plen;
    initCalibrationPanel(plen);

    // USB (WebSerial) - sends "bridge on", then home pose
    window.connectRobot = async () => {
        try {
            await plen.connectSerial();
            console.log("USB connected");
        } catch (e) {
            console.error("USB connect failed:", e);
            alert("USB connection failed: " + (e?.message || e));
        }
    };

    // BLE (WebBluetooth, Nordic UART)
    window.connectRobotBle = async () => {
        try {
            await plen.connectBle();
            await plen.sendHomePose("BLE");
            console.log("BLE connected");
        } catch (e) {
            console.error("BLE connect failed:", e);
            alert("BLE connection failed: " + e);
        }
    };

    // WiFi WebSocket (ws://<ip>/ws)
    window.connectRobotWifi = async () => {
        const ip = prompt("Enter robot IP address (printed in the Serial Monitor at boot, e.g. 192.168.x.x):");
        if (!ip) return;
        try {
            await plen.connectWifi(ip);
            await plen.sendHomePose("WiFi");
            console.log("WiFi connected");
        } catch (e) {
            console.error("WiFi connect failed:", e);
            alert("WiFi connection failed: " + e);
        }
    };

    // Start/Stop Sync: play the timeline with the robot following it
    window.startSync = () => {
        if (!rig || !plen.isTransportReady()) {
            console.warn("Cannot start sync - robot not connected or rig not ready.");
            return;
        }
        Timeline.playing  = true;
        Timeline.elapsed  = 0;
        Timeline.lastTime = performance.now();
        console.log("Robohon sync started.");
        plen.sendFullFrame(rig.getAngleMap());
        renderFrameTimeline();
    };

    window.stopSync = () => {
        Timeline.playing = false;
        console.log("Robohon sync stopped.");
        renderFrameTimeline();
    };

    /* =========================================================
       OPEN MOTION (file input handler)
       ========================================================= */

    document.getElementById("open-motion-input").addEventListener("change", (e) => {
        const file = e.target.files[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = (ev) => {
            try {
                const motion = JSON.parse(ev.target.result);
                importMotion(motion);
            } catch (err) {
                console.error("Failed to parse motion file:", err);
                alert("Invalid motion file — could not parse JSON.");
            }
        };
        reader.readAsText(file);

        // Reset so the same file can be re-opened if needed
        e.target.value = "";
    });

    /* =========================================================
       MOTION PROPERTIES DIALOG
       ========================================================= */

    window.openMotionProps = () => {
        document.getElementById("prop-slot").value       = motionProps.slot;
        document.getElementById("prop-name").value       = motionProps.name;

        const loopCb = document.getElementById("prop-loop-enabled");
        loopCb.checked = motionProps.loop.enabled;
        document.getElementById("loop-section").classList.toggle("disabled", !motionProps.loop.enabled);
        document.getElementById("prop-loop-begin").value = motionProps.loop.begin;
        document.getElementById("prop-loop-end").value   = motionProps.loop.end;
        document.getElementById("prop-loop-count").value = motionProps.loop.count;

        const jumpCb = document.getElementById("prop-jump-enabled");
        jumpCb.checked = motionProps.jump.enabled;
        document.getElementById("jump-section").classList.toggle("disabled", !motionProps.jump.enabled);
        document.getElementById("prop-jump-begin").value = motionProps.jump.begin;
        document.getElementById("prop-jump-end").value   = motionProps.jump.end;
        document.getElementById("prop-jump-slot").value  = motionProps.jump.slot;

        document.getElementById("motion-props-overlay").classList.add("open");
    };

    window.closeMotionProps = () => {
        document.getElementById("motion-props-overlay").classList.remove("open");
    };

    window.saveMotionProps = () => {
        motionProps.slot = parseInt(document.getElementById("prop-slot").value, 10)  || 0;
        motionProps.name = document.getElementById("prop-name").value.trim()          || "New Motion";

        motionProps.loop.enabled = document.getElementById("prop-loop-enabled").checked;
        motionProps.loop.begin   = parseInt(document.getElementById("prop-loop-begin").value, 10) || 0;
        motionProps.loop.end     = parseInt(document.getElementById("prop-loop-end").value,   10) || 0;
        motionProps.loop.count   = parseInt(document.getElementById("prop-loop-count").value, 10) || 255;

        motionProps.jump.enabled = document.getElementById("prop-jump-enabled").checked;
        motionProps.jump.begin   = parseInt(document.getElementById("prop-jump-begin").value, 10) || 0;
        motionProps.jump.end     = parseInt(document.getElementById("prop-jump-end").value,   10) || 0;
        motionProps.jump.slot    = parseInt(document.getElementById("prop-jump-slot").value,  10) || 0;

        window.closeMotionProps();
    };

    document.getElementById("motion-props-overlay").addEventListener("click", (e) => {
        if (e.target === document.getElementById("motion-props-overlay")) {
            window.closeMotionProps();
        }
    });

    /* --- EXPORT --- */
    window.exportMotion = () => {
        const motion = Exporter.exportMotion(Timeline, {
            frameLength: 10,
            slot:        motionProps.slot,
            name:        motionProps.name,
            loop:        motionProps.loop,
            jump:        motionProps.jump
        });
        Exporter.downloadMotion(motion, "motion.json");
    };

    /* =========================================================
       FRAME CONTROL BUTTONS
       ========================================================= */

    window.nextFrame = () => {
        Timeline.playing = false;
        Timeline.elapsed = 0;
        Timeline.lastTime = performance.now();

        Timeline.nextFrame();
        snapRobotToCurrentFrame();
        renderFrameTimeline();
    };

    window.prevFrame = () => {
        Timeline.playing = false;
        Timeline.elapsed = 0;
        Timeline.lastTime = performance.now();

        Timeline.prevFrame();
        snapRobotToCurrentFrame();
        renderFrameTimeline();
    };

    window.togglePlay = () => {
        Timeline.playing = !Timeline.playing;
        Timeline.elapsed = 0;
        Timeline.lastTime = performance.now();
        renderFrameTimeline();
    };

    window.addEventListener("resize", onWindowResize);
}

/* =========================================================
   IMPORT MOTION
   Parses a PLEN motion JSON and rebuilds Timeline + motionProps.
   ========================================================= */

function importMotion(motion) {
    if (!rig) {
        alert("Robot not loaded yet — wait for the model to finish loading.");
        return;
    }

    if (!motion.frames || !Array.isArray(motion.frames) || motion.frames.length === 0) {
        alert("Motion file has no frames.");
        return;
    }
	
	 Timeline.motionProps = motionProps;

    // Stop playback
    Timeline.playing = false;
    Timeline.elapsed = 0;
    Timeline.lastTime = performance.now();
    transformControls.detach();

    // --- Restore motionProps from file ---
    motionProps.slot = motion.slot ?? 0;
    motionProps.name = motion.name ?? "New Motion";

    // Reset loop/jump to off, then re-enable if present in codes
    motionProps.loop = { enabled: false, begin: 0, end: 0, count: 255 };
    motionProps.jump = { enabled: false, begin: 0, end: 0, slot: 0  };

    if (Array.isArray(motion.codes)) {
        for (const code of motion.codes) {
            if (code.method === "loop" && Array.isArray(code.arguments)) {
                motionProps.loop.enabled = true;
                motionProps.loop.begin   = code.arguments[0] ?? 0;
                motionProps.loop.end     = code.arguments[1] ?? 0;
                motionProps.loop.count   = code.arguments[2] ?? 255;
            }
            if (code.method === "jump" && Array.isArray(code.arguments)) {
                motionProps.jump.enabled = true;
                motionProps.jump.begin   = code.arguments[0] ?? 0;
                motionProps.jump.end     = code.arguments[1] ?? 0;
                motionProps.jump.slot    = code.arguments[2] ?? 0;
            }
        }
    }

    // --- Rebuild Timeline.frames ---
    Timeline.frames       = [];
    Timeline.currentFrame = 0;

    for (const srcFrame of motion.frames) {
        // Build a canonical angle map: start from all-zero for every known
        // servo, then overwrite with whatever the file contains. This means
        // missing servos default to 0 rather than crashing.
        const angles = {};
        for (const name in ServoRig) {
            angles[name] = 0;
        }

        if (Array.isArray(srcFrame.outputs)) {
            for (const output of srcFrame.outputs) {
                if (output.device && angles[output.device] !== undefined) {
                    angles[output.device] = output.value ?? 0;
                }
            }
        }

        Timeline.frames.push({
            index:      Timeline.frames.length,
            angles:     angles,
            transition: srcFrame.transition_time_ms ?? 300,
            thumbnail:  ""
        });
    }

    // Snap robot to frame 0
    Timeline.currentFrame = 0;
    snapRobotToCurrentFrame();

    // Render the timeline strip first (shows blank thumbnails immediately)
    renderFrameTimeline();

    // Then capture thumbnails for every frame sequentially.
    // Each frame: apply pose → render → snapshot → restore current frame.
    // Done in a rAF chain so the UI isn't blocked.
    captureThumbnailsSequentially(0, () => {
        // Snap back to frame 0 and do a final render once all thumbnails done
        Timeline.currentFrame = 0;
        snapRobotToCurrentFrame();
        renderFrameTimeline();
    });

    console.log(`Imported "${motionProps.name}" — ${Timeline.frames.length} frames`);
}

/* =========================================================
   SEQUENTIAL THUMBNAIL CAPTURE
   Walks every frame, applies its pose, renders, snapshots.
   Uses rAF so each frame gets a real render tick.
   ========================================================= */

function captureThumbnailsSequentially(index, onComplete) {
    if (index >= Timeline.frames.length) {
        if (onComplete) onComplete();
        return;
    }

    rig.applyAngleMap(Timeline.frames[index].angles);

    requestAnimationFrame(() => {
        captureFrameThumbnail(index);
        renderFrameTimeline();
        captureThumbnailsSequentially(index + 1, onComplete);
    });
}

/* =========================================================
   LOAD ROBOT
   ========================================================= */

function loadRobot() {
    gltfLoader.load(
        "assets/glb/robot_v2.glb",

        (gltf) => {
            model = gltf.scene;

            model.traverse((obj) => {
                if (obj.isMesh) {
                    obj.castShadow = true;
                    obj.receiveShadow = true;
                    if (obj.material) obj.material.side = DoubleSide;
                }
            });

            Joints.applyTags(model);

            root.add(model);

            const box = new Box3().setFromObject(model);
            model.position.y -= box.min.y;

            rig = new RigController(model);
            window.rig = rig;

            Timeline.addFrame(rig);

            const fullBox = new Box3().setFromObject(root);
            const center = new Vector3();
            fullBox.getCenter(center);

            orbitControls.target.copy(center);

            const size = fullBox.getSize(new Vector3()).length();

            camera.position.set(
                center.x,
                center.y + size * 0.5,
                center.z + size * 2.5
            );

            orbitControls.update();

            window.model = model;
            window.ServoRig = ServoRig;

            // Defer thumbnail two ticks so camera damping settles first
            requestAnimationFrame(() => {
                requestAnimationFrame(() => {
                    captureFrameThumbnail(0);
                    renderFrameTimeline();
                });
            });
        },

        undefined,
        (err) => console.error("GLB load failed:", err)
    );
}

/* =========================================================
   SNAP ROBOT TO SELECTED FRAME
   ========================================================= */

function snapRobotToCurrentFrame() {
    const i = Timeline.currentFrame;
    if (i == null || !Timeline.frames[i]) return;

    rig.applyAngleMap(Timeline.frames[i].angles);

    // Selecting a frame also moves the real servos (when connected).
    if (plen && plen.isTransportReady()) {
        plen.sendFullFrame(Timeline.frames[i].angles);
    }
}

/* =========================================================
   FRAME THUMBNAILS + TIMELINE UI
   ========================================================= */

function captureFrameThumbnail(frameIndex) {
    const frame = Timeline.frames[frameIndex];
    if (!frame) return;

    const wasVisible = transformControls.visible;
    transformControls.visible = false;

    renderer.render(scene, camera);

    try {
        frame.thumbnail = renderer.domElement.toDataURL("image/png");
    } catch (e) {
        console.warn("Thumbnail capture failed:", e);
    }

    transformControls.visible = wasVisible;
}

function renderFrameTimeline() {
    if (!frameTimelineEl || !Timeline.frames) return;

    frameTimelineEl.innerHTML = "";

    const currentIndex = Timeline.currentFrame;

    Timeline.frames.forEach((frame, index) => {
        const item = document.createElement("div");
        item.className = "frame-item";
        if (index === currentIndex) item.classList.add("selected");

        const img = document.createElement("img");
        img.className = "frame-thumb";
        img.src = frame.thumbnail || "";
        item.appendChild(img);

        const meta = document.createElement("div");
        meta.className = "frame-meta";
        meta.textContent = `Frame: ${index}`;
        item.appendChild(meta);

        const trans = document.createElement("div");
        trans.className = "frame-meta";
        const transitionValue = frame.transition ?? 300;
        trans.textContent = `Transition: ${transitionValue} ms`;
        item.appendChild(trans);

        const slider = document.createElement("input");
        slider.type  = "range";
        slider.className = "frame-slider";
        slider.min   = "50";
        slider.max   = "5000";
        slider.step  = "10";
        slider.value = transitionValue.toString();

        slider.addEventListener("input", (e) => {
            const val = parseInt(e.target.value, 10) || 300;
            frame.transition = val;
            trans.textContent = `Transition: ${val} ms`;
        });

        item.appendChild(slider);

        item.addEventListener("click", () => {
            Timeline.playing = false;
            Timeline.elapsed = 0;
            Timeline.lastTime = performance.now();

            Timeline.currentFrame = index;
            snapRobotToCurrentFrame();
            renderFrameTimeline();
        });

        frameTimelineEl.appendChild(item);
    });

    // ADD FRAME BUTTON
    const addBtn = document.createElement("div");
    addBtn.className = "add-frame-button";
    addBtn.textContent = "+";

    addBtn.addEventListener("click", () => {
        if (!rig) return;

        Timeline.playing = false;
        Timeline.elapsed = 0;
        Timeline.lastTime = performance.now();

        const idx = Timeline.addFrame(rig);
        Timeline.currentFrame = idx;

        captureFrameThumbnail(idx);
        snapRobotToCurrentFrame();
        renderFrameTimeline();
    });

    frameTimelineEl.appendChild(addBtn);
}

/* =========================================================
   POINTER PICKING
   ========================================================= */

function onPointerDown(event) {

    if (transformControls.dragging) return;

    const rect = renderer.domElement.getBoundingClientRect();
    mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

    raycaster.setFromCamera(mouse, camera);

    const joints = [];
    root.traverse(obj => {
        if (ServoRig[obj.name]) joints.push(obj);
    });

    const intersects = raycaster.intersectObjects(joints, true);

    if (intersects.length > 0) {
        let joint = intersects[0].object;

        while (joint && !ServoRig[joint.name] && joint.parent) {
            joint = joint.parent;
        }

        if (joint && ServoRig[joint.name]) {
            transformControls.attach(joint);

            const axis = ServoRig[joint.name].axis;
            transformControls.showX = axis[0] !== 0;
            transformControls.showY = axis[1] !== 0;
            transformControls.showZ = axis[2] !== 0;

            return;
        }
    }

    transformControls.detach();
}

/* =========================================================
   RESIZE
   ========================================================= */

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
}

/* =========================================================
   ANIMATE
   ========================================================= */

function animate() {
    requestAnimationFrame(animate);

    orbitControls.update();

    if (rig && Timeline.playing) {
        Timeline.update(rig);

        const now = performance.now();
        if (plen && plen.isTransportReady()) {
            if (now - _lastSendTime >= SEND_INTERVAL_MS) {
                _lastSendTime = now;
                plen.sendFullFrame(rig.getAngleMap());
            }
        }
    }

    renderer.render(scene, camera);
	
	window.Timeline = Timeline;
window.motionProps = motionProps;
}