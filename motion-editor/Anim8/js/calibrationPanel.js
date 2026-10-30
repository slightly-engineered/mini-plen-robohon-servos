// js/calibrationPanel.js
// Small on-screen panel for finding each servo's HOME and DIRECTION on the
// mini PLEN. Values persist in localStorage and can be pasted into
// core/robohonMap.js when you are happy with them.
import {
    HW, saveCalibration, resetCalibration, calibrationDump, angleToRaw
} from "../core/robohonMap.js";

export function initCalibrationPanel(controller) {

    const panel = document.createElement("div");
    panel.id = "cal-panel";
    panel.style.cssText = [
        "position:fixed", "top:20px", "right:20px", "z-index:9999",
        "width:300px", "padding:12px", "display:none",
        "background:rgba(20,24,30,0.95)", "color:#ddd",
        "font:12px Arial,sans-serif", "border:1px solid #445",
        "border-radius:6px"
    ].join(";");

    const btn = "padding:5px 8px;font-size:12px;margin:2px;";

    panel.innerHTML = `
        <div style="font-weight:bold;font-size:13px;margin-bottom:8px;">Servo Calibration</div>
        <select id="cal-servo" style="width:100%;margin-bottom:6px;"></select>
        <div id="cal-info" style="margin-bottom:6px;line-height:1.5;"></div>
        <div>
            <button data-n="-1000" style="${btn}">-1000</button>
            <button data-n="-100"  style="${btn}">-100</button>
            <button data-n="-10"   style="${btn}">-10</button>
            <button data-n="10"    style="${btn}">+10</button>
            <button data-n="100"   style="${btn}">+100</button>
            <button data-n="1000"  style="${btn}">+1000</button>
        </div>
        <div style="margin:6px 0;">
            Raw: <input id="cal-raw" type="number" step="1" style="width:90px;">
            <button id="cal-send" style="${btn}">Send</button>
        </div>
        <div>
            <button id="cal-home"  style="${btn}">Set Home = current raw</button>
            <button id="cal-flip"  style="${btn}">Flip Dir</button>
            <button id="cal-model" style="${btn}">Back to model</button>
            <button id="cal-reset" style="${btn}">Reset all</button>
        </div>
        <textarea id="cal-dump" readonly rows="8"
            style="width:100%;margin-top:6px;font:11px monospace;background:#0d0f12;color:#9fd;"></textarea>
        <div style="margin-top:4px;opacity:0.7;">
            Put the joint at 0&deg; in Anim8, nudge until the real joint sits at
            the model's zero pose, then Set Home.
        </div>`;
    document.body.appendChild(panel);

    const $ = (id) => panel.querySelector(id);
    const sel = $("#cal-servo");

    for (const name in HW) {
        const c = HW[name];
        const o = document.createElement("option");
        o.value = name;
        o.textContent = `${name}  (${c.tag}: bus ${c.bus} / SID ${c.sid})`;
        sel.appendChild(o);
    }

    function refresh() {
        const name = sel.value;
        const c = HW[name];
        if (!c) return;
        const raw = controller.getRaw(name);
        $("#cal-info").innerHTML =
            `${c.robohon} &rarr; bus ${c.bus}, SID ${c.sid}<br>` +
            `home <b>${c.home}</b> &nbsp; dir <b>${c.dir > 0 ? "+1" : "-1"}</b> &nbsp; ` +
            `now <b>${raw}</b>` +
            (controller.rawHold[name] !== undefined ? " (held)" : "");
        $("#cal-raw").value = raw;
        $("#cal-dump").value = calibrationDump();
    }

    sel.addEventListener("change", refresh);

    panel.querySelectorAll("button[data-n]").forEach(b => {
        b.addEventListener("click", () => {
            const name = sel.value;
            controller.sendRaw(name, controller.getRaw(name) + parseInt(b.dataset.n, 10));
            refresh();
        });
    });

    $("#cal-send").addEventListener("click", () => {
        const v = parseInt($("#cal-raw").value, 10);
        if (Number.isFinite(v)) controller.sendRaw(sel.value, v);
        refresh();
    });

    $("#cal-home").addEventListener("click", () => {
        const name = sel.value;
        HW[name].home = controller.getRaw(name);
        saveCalibration();
        refresh();
    });

    $("#cal-flip").addEventListener("click", () => {
        const name = sel.value;
        HW[name].dir = -HW[name].dir;
        saveCalibration();
        controller.releaseRaw(name);   // re-send at the model angle with the new sign
        refresh();
    });

    $("#cal-model").addEventListener("click", () => {
        controller.releaseRaw(sel.value);
        refresh();
    });

    $("#cal-reset").addEventListener("click", () => {
        if (!confirm("Reset ALL servo homes/directions to factory values?")) return;
        resetCalibration();
        for (const n in HW) controller.releaseRaw(n);
        refresh();
    });

    window.toggleCalibration = () => {
        panel.style.display = panel.style.display === "none" ? "block" : "none";
        if (panel.style.display === "block") refresh();
    };
}
