// rigController.js (ESM)
// Drift‑free servo‑angle rig controller using axis‑projection math.

import * as THREE from "../js/three.module.js";
import { ServoRig, jointSign } from "./servoRig.js";

export class RigController {

    constructor(root) {
        this.root = root;

        // Maps servoName → THREE.Object3D
        this.nodes = {};

        // Maps servoName → home quaternion
        this.home = {};

        // Maps servoName → THREE.Vector3 axis (normalized)
        this.axis = {};

        this._init();
    }

    _init() {
        // Find GLB nodes and capture home quaternions
        for (const name in ServoRig) {
            const cfg = ServoRig[name];
            const node = this.root.getObjectByName(cfg.node);

            if (!node) {
                console.warn("Servo node missing:", cfg.node);
                continue;
            }

            this.nodes[name] = node;
            this.home[name] = node.quaternion.clone();

            const axis = new THREE.Vector3(...cfg.axis);
            axis.normalize();
            this.axis[name] = axis;
        }
    }

    // ------------------------------------------------------------
    // Extract servo angle from GLB node (drift‑free)
    // ------------------------------------------------------------
    getAngle(name) {
        const node = this.nodes[name];
        if (!node) return 0;

        const home = this.home[name];
        const current = node.quaternion;

        // Relative rotation: home⁻¹ * current
        const rel = home.clone().invert().multiply(current);

        // Project quaternion onto servo axis
        const axis = this.axis[name];

        // Extract angle around axis using quaternion vector component
        const v = new THREE.Vector3(rel.x, rel.y, rel.z);
        const proj = v.dot(axis); // scalar projection

        const theta_half = Math.atan2(proj, rel.w);
        let theta = theta_half * 2;

        // Apply direction sign
        if (jointSign[name] === -1) theta = -theta;

        // Convert rad → PLEN units (0.1 deg)
        return Math.round(theta * 1800 / Math.PI);
    }

    // ------------------------------------------------------------
    // Apply servo angle to GLB node (drift‑free)
    // ------------------------------------------------------------
    setAngle(name, angle) {
        const cfg = ServoRig[name];
        const node = this.nodes[name];
        if (!node) return;

        // Clamp
        angle = Math.max(cfg.min, Math.min(cfg.max, angle));

        // Convert PLEN units → radians
        let theta = angle * Math.PI / 1800;

        // Apply direction sign
        if (jointSign[name] === -1) theta = -theta;

        // Build quaternion from axis
        const q = new THREE.Quaternion();
        q.setFromAxisAngle(this.axis[name], theta);

        // Apply relative to home
        const home = this.home[name];
        node.quaternion.copy(home).multiply(q);
    }

    // ------------------------------------------------------------
    // Extract angle from a dragged Object3D (same math as getAngle)
    // ------------------------------------------------------------
    updateFromObject3D(name, joint) {
        if (!this.nodes[name]) return 0;

        const home = this.home[name];
        const current = joint.quaternion;

        const rel = home.clone().invert().multiply(current);
        const axis = this.axis[name];

        const v = new THREE.Vector3(rel.x, rel.y, rel.z);
        const proj = v.dot(axis);

        const theta_half = Math.atan2(proj, rel.w);
        let theta = theta_half * 2;

        if (jointSign[name] === -1) theta = -theta;

        let angle = Math.round(theta * 1800 / Math.PI);

        // Clamp
        const cfg = ServoRig[name];
        angle = Math.max(cfg.min, Math.min(cfg.max, angle));

        return angle;
    }

    // ------------------------------------------------------------
    // Extract all servo angles (for saving a frame)
    // ------------------------------------------------------------
    getAngleMap() {
        const map = {};
        for (const name in this.nodes) {
            map[name] = this.getAngle(name);
        }
        return map;
    }

    // ------------------------------------------------------------
    // Apply a saved angle map (for snapping to a frame)
    // Accepts:
    //   { servoName: angle }  OR  [angle0, angle1, ...]
    // ------------------------------------------------------------
    applyAngleMap(map) {
        if (!map) return;

        // Array form (native order)
        if (Array.isArray(map)) {
            let i = 0;
            for (const name in this.nodes) {
                this.setAngle(name, map[i]);
                i++;
            }
            return;
        }

        // Object form (servoName → angle)
        for (const name in map) {
            if (this.nodes[name]) {
                this.setAngle(name, map[name]);
            }
        }
    }

    // ------------------------------------------------------------
    // Apply native PLEN angle array
    // ------------------------------------------------------------
    applyNativeAngles(angleArray) {
        if (!Array.isArray(angleArray)) return;

        let i = 0;
        for (const name in this.nodes) {
            const angle = angleArray[i];
            this.setAngle(name, angle);
            i++;
        }
    }
}
