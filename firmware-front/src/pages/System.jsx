import { useEffect, useRef, useState } from "preact/hooks";
import { get, post, upload } from "../api.js";
import { AsyncButton, Spinner, confirmDialog, run, toast, usePoll } from "../components/ui.jsx";
import { bytes, duration } from "../util.js";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function SystemPage() {
    const [info, setInfo] = useState(null);
    const [files, setFiles] = useState(null);

    async function loadInfo(quiet) {
        // Background refreshes stay quiet: the header banner already says when the device is offline.
        try { setInfo(await get("/api/dashboard/get")); } catch (e) { if (!quiet) toast(e.message, true); }
    }
    async function loadFiles() {
        try { setFiles(await get("/api/files/get")); } catch (e) { toast(e.message, true); }
    }
    useEffect(() => { loadFiles(); }, []);
    usePoll(() => loadInfo(true), 10000); // keeps uptime and memory current during a long run

    return (
        <>
            <DeviceCard info={info} reload={() => loadInfo(false)} />
            <FirmwareCard files={files} reloadFiles={loadFiles} />
            <FilesCard files={files} reload={loadFiles} />
            <MaintenanceCard />
        </>
    );
}

function DeviceCard({ info, reload }) {
    return (
        <section class="card">
            <div class="head">
                <h2>Device</h2>
                <button class="ghost small" onClick={reload}>Refresh</button>
            </div>
            {!info ? <p class="muted"><Spinner /> Loading…</p> : (
                <dl class="kv">
                    <dt>Firmware</dt><dd>{info.firmwareName} {info.firmwareVer}</dd>
                    <dt>Device name</dt><dd>{info.deviceName}</dd>
                    <dt>Sensor</dt><dd>{info.sensorModel === 22 ? "DHT22" : info.sensorModel === 11 ? "DHT11" : info.sensorModel}{info.sensorErrors > 0 && <span class="muted"> · {info.sensorErrors} failed reads</span>}</dd>
                    <dt>Uptime</dt><dd>{duration(info.uptime)}</dd>
                    <dt>Free memory</dt><dd>{bytes(info.freeHeap)}{info.minFreeHeap !== undefined && <span class="muted"> · lowest {bytes(info.minFreeHeap)}, largest block {bytes(info.maxFreeBlock)}</span>}</dd>
                    <dt>Address</dt><dd>{info.networkAddress || "–"}</dd>
                    {info.resetReason && <><dt>Last restart</dt><dd>{info.restartNote || info.resetReason}{info.resetInfo && <div class="help">{info.resetInfo}</div>}</dd></>}
                </dl>
            )}
        </section>
    );
}

// Upload a .bin, then flash it. Status codes: 255 idle, 254 busy, 1 done, 0 failed.
function FirmwareCard({ files, reloadFiles }) {
    const [step, setStep] = useState("pick"); // pick | uploading | ready | flashing | done | failed
    const [progress, setProgress] = useState(0);
    const [name, setName] = useState("");
    const input = useRef(null);
    const bins = files ? files.files.filter((f) => f.toLowerCase().endsWith(".bin")) : [];

    async function onPick(e) {
        const file = e.currentTarget.files[0];
        e.currentTarget.value = "";
        if (!file) return;
        if (!file.name.toLowerCase().endsWith(".bin")) return toast("Pick a firmware .bin file", true);
        if (files && file.size > Number(files.max) - Number(files.used)) return toast(`Not enough space: the file is ${bytes(file.size)}, free space is ${bytes(Number(files.max) - Number(files.used))}. Delete old files first.`, true);
        setStep("uploading");
        setProgress(0);
        try {
            await upload(file, setProgress);
            setName(file.name);
            setStep("ready");
            reloadFiles();
        } catch (err) {
            toast(err.message, true);
            setStep("pick");
        }
    }

    async function flash(fname) {
        const ok = await confirmDialog({
            title: `Install ${fname}?`,
            body: "Keep the device powered during the update. It takes about a minute. A recording in progress is saved first.",
            confirm: "Install firmware",
            danger: true,
        });
        if (!ok) return;
        setName(fname);
        if (!(await run(() => post("/api/update?filename=" + encodeURIComponent(fname))))) return;
        setStep("flashing");
        for (let i = 0; i < 120; i++) {
            await sleep(1500);
            let s;
            try { s = (await get("/api/update-status")).status; } catch { continue; }
            if (s === 1) return setStep("done");
            if (s === 0) return setStep("failed");
        }
        setStep("failed");
    }

    async function restart() {
        if (await run(() => post("/api/restart"), "Restarting… the page reconnects by itself.")) setStep("pick");
    }

    return (
        <section class="card">
            <h2>Firmware update</h2>
            <div class="steps">
                <span class={step === "pick" || step === "uploading" ? "on" : "done"}>Upload</span>
                <span class={step === "ready" || step === "flashing" ? "on" : step === "done" || step === "failed" ? "done" : ""}>Install</span>
                <span class={step === "done" ? "on" : ""}>Restart</span>
            </div>
            {step === "pick" && (
                <>
                    <p class="muted">Upload a firmware <code>.bin</code> built for this board, or install one already on the device.</p>
                    <input ref={input} type="file" accept=".bin" hidden onChange={onPick} />
                    <div class="row">
                        <button onClick={() => input.current.click()}>Choose .bin file…</button>
                        {bins.map((b) => <button key={b} class="secondary" onClick={() => flash(b)}>Install {b}</button>)}
                    </div>
                </>
            )}
            {step === "uploading" && (
                <>
                    <p>Uploading… {Math.round(progress * 100)}%</p>
                    <div class="progress"><div style={{ width: `${progress * 100}%` }} /></div>
                </>
            )}
            {step === "ready" && (
                <div class="row">
                    <button class="danger" onClick={() => flash(name)}>Install {name}</button>
                    <button class="ghost" onClick={() => setStep("pick")}>Cancel</button>
                </div>
            )}
            {step === "flashing" && <p><Spinner /> Installing {name}. Don't power off the device…</p>}
            {step === "done" && (
                <>
                    <p class="ok-text">Installed. Restart the device to run the new firmware.</p>
                    <AsyncButton onClick={restart}>Restart now</AsyncButton>
                </>
            )}
            {step === "failed" && (
                <>
                    <p class="err-text">The update failed. The current firmware is still running. Check that the file is a firmware image for this board.</p>
                    <button class="secondary" onClick={() => setStep("pick")}>Try again</button>
                </>
            )}
        </section>
    );
}

function FilesCard({ files, reload }) {
    const [progress, setProgress] = useState(null);
    const input = useRef(null);

    async function remove(f) {
        const ok = await confirmDialog({ title: `Delete ${f}?`, body: "This can't be undone.", confirm: "Delete", danger: true });
        if (ok && (await run(() => post("/api/files/remove?filename=" + encodeURIComponent(f)), `${f} deleted`))) reload();
    }

    async function onPick(e) {
        const file = e.currentTarget.files[0];
        e.currentTarget.value = "";
        if (!file) return;
        setProgress(0);
        try {
            await upload(file, setProgress);
            toast(`${file.name} uploaded`);
            reload();
        } catch (err) {
            toast(err.message, true);
        } finally {
            setProgress(null);
        }
    }

    const used = files ? Number(files.used) : 0;
    const max = files ? Number(files.max) : 1;
    return (
        <section class="card">
            <div class="head">
                <h2>Files</h2>
                <div class="row">
                    <input ref={input} type="file" hidden onChange={onPick} />
                    <button class="secondary small" disabled={progress !== null} onClick={() => input.current.click()}>Upload…</button>
                </div>
            </div>
            {!files ? <p class="muted"><Spinner /> Loading…</p> : (
                <>
                    <p class="muted">Storage: {bytes(used)} of {bytes(max)} used. Recorded sessions are listed on the Logger page.</p>
                    <div class="progress" title={`${Math.round((used / max) * 100)}% used`}><div style={{ width: `${(used / max) * 100}%` }} /></div>
                    {progress !== null && <p>Uploading… {Math.round(progress * 100)}%</p>}
                    {files.files.length === 0 ? <p class="muted">No files.</p> : (
                        <div class="tablewrap">
                            <table>
                                <tbody>
                                    {files.files.map((f) => (
                                        <tr key={f}>
                                            <td class="name">{f}</td>
                                            <td class="num"><div class="rowbtns">
                                                <a class="btn ghost small" href={"/download/" + encodeURIComponent(f)} download={f}>Download</a>
                                                <button class="ghost small danger-text" onClick={() => remove(f)}>Delete</button>
                                            </div></td>
                                        </tr>
                                    ))}
                                </tbody>
                            </table>
                        </div>
                    )}
                </>
            )}
        </section>
    );
}

function MaintenanceCard() {
    async function restart() {
        if (await confirmDialog({ title: "Restart the device?", body: "A recording in progress is saved and continues after the restart.", confirm: "Restart" }))
            await run(() => post("/api/restart"), "Restarting… the page reconnects by itself.");
    }
    async function reset() {
        const ok = await confirmDialog({
            title: "Reset to factory defaults?",
            body: "All settings, including WiFi and the admin password, go back to defaults. The device restarts with its own hotspot. Recorded sessions stay on the device.",
            confirm: "Factory reset",
            danger: true,
        });
        if (ok) await run(() => post("/api/reset"), "Resetting… connect to the device hotspot to set it up again.");
    }
    return (
        <section class="card">
            <h2>Maintenance</h2>
            <div class="row">
                <button class="secondary" onClick={restart}>Restart device</button>
                <button class="danger" onClick={reset}>Factory reset…</button>
            </div>
        </section>
    );
}
