import { useEffect, useRef, useState } from "preact/hooks";
import { get, post } from "../api.js";
import { Chart } from "../components/Chart.jsx";
import { AsyncButton, Field, confirmDialog, run, toast } from "../components/ui.jsx";
import { dateTime, duration } from "../util.js";

const MAX_POINTS = 1500;

function Stats({ s }) {
    if (!s || !s.temp) return null;
    const items = [
        ["Temp min", s.temp.min, "°C"], ["Temp avg", (+s.temp.avg).toFixed(1), "°C"], ["Temp max", s.temp.max, "°C"],
        ["Hum min", s.hum.min, "%"], ["Hum avg", (+s.hum.avg).toFixed(1), "%"], ["Hum max", s.hum.max, "%"],
    ];
    return <div class="stats">{items.map(([l, v, u]) => <div class="stat" key={l}><span class="label">{l}</span><b>{v} {u}</b></div>)}</div>;
}

function sessionDuration(x, status) {
    if (x.active && status && status.session) return status.session.elapsed;
    if (x.endEpoch && x.startEpoch) return x.endEpoch - x.startEpoch;
    return (x.count * x.intervalMs) / 1000;
}

function StartForm({ status, onStarted }) {
    const [name, setName] = useState("");
    const [notes, setNotes] = useState("");
    const [rate, setRate] = useState("");
    const rateValue = rate === "" ? (status ? status.sampleRate / 1000 : 5) : Number(rate);
    const rateError = !(rateValue >= 2 && rateValue <= 3600) ? "Between 2 and 3600 seconds" : null;
    const recs = status ? status.storage.capacityRecords : 0;

    async function start() {
        if (rateError) return;
        if (status && !status.time.valid) toast("The device clock is not set yet. Readings will get timestamps once it is.", true);
        const ok = await run(() => post("/api/session/start", { name: name.trim(), notes: notes.trim(), rate: Math.round(rateValue * 1000) }), "Recording started");
        if (ok) { setName(""); setNotes(""); setRate(""); onStarted(); }
    }

    return (
        <>
            <div class="row">
                <Field id="sname" label="Name">
                    <input id="sname" maxLength={48} value={name} placeholder="e.g. Incubator test 3" onInput={(e) => setName(e.currentTarget.value)} />
                </Field>
                <Field id="srate" label="Interval (seconds)" narrow error={rate !== "" && rateError}>
                    <input id="srate" type="number" min="2" max="3600" step="1" value={rate} placeholder={String(rateValue)}
                        aria-invalid={rate !== "" && rateError ? "true" : undefined} onInput={(e) => setRate(e.currentTarget.value)} />
                </Field>
            </div>
            <div style="margin-top:12px">
                <Field id="snotes" label="Notes (optional)">
                    <textarea id="snotes" maxLength={200} value={notes} onInput={(e) => setNotes(e.currentTarget.value)} />
                </Field>
            </div>
            <div class="actions"><AsyncButton onClick={start} disabled={!status || !!rateError}>Start recording</AsyncButton></div>
            {status && (
                <p class="help">
                    Space for about {recs.toLocaleString()} more readings ({duration((recs * rateValue))} at {rateValue} s).
                </p>
            )}
        </>
    );
}

export function LoggerPage({ status, refresh }) {
    const [sessions, setSessions] = useState(null);
    const [view, setView] = useState(null); // {id, name}
    const [rows, setRows] = useState([]);
    const cursor = useRef({ id: 0, next: 0, step: 1, rows: [] });
    const lastActive = useRef(undefined);

    async function loadSessions() {
        try {
            const d = await get("/api/sessions");
            setSessions(d.sessions.sort((a, b) => b.id - a.id));
            return d.sessions;
        } catch (e) {
            toast(e.message, true);
            return [];
        }
    }

    async function fetchRows(reset) {
        const c = cursor.current;
        if (!c.id) return;
        try {
            for (let guard = 0; guard < 20; guard++) {
                const d = await get(`/api/session/data?id=${c.id}&from=${c.next}&step=${c.step}&max=${MAX_POINTS}`);
                if (c !== cursor.current) return; // switched sessions meanwhile
                c.rows = reset && guard === 0 ? d.rows : c.rows.concat(d.rows);
                c.next = d.next;
                if (d.next >= d.count || !d.rows.length) break;
            }
            if (c.rows.length > MAX_POINTS * 2) { c.rows = c.rows.filter((_, i) => i % 2 === 0); c.step *= 2; }
            setRows(c.rows);
        } catch (e) {
            toast(e.message, true);
        }
    }

    async function show(s) {
        try {
            const first = await get(`/api/session/data?id=${s.id}&from=0&max=1`);
            cursor.current = { id: s.id, next: 0, step: Math.max(1, Math.ceil(first.count / MAX_POINTS)), rows: [] };
            setView({ id: s.id, name: s.name });
            setRows([]);
            await fetchRows(true);
        } catch (e) {
            toast(e.message, true);
        }
    }

    useEffect(() => { loadSessions(); }, []);

    // React to each status update: follow the running session, refresh lists.
    useEffect(() => {
        if (!status) return;
        const was = lastActive.current;
        lastActive.current = status.active;
        if (status.active && (!view || was === false || was === undefined)) {
            if (!view || view.id !== status.session.id) show(status.session);
        } else if (status.active && view && view.id === status.session.id) {
            fetchRows(false);
        }
        if (was !== undefined && was !== status.active) loadSessions();
    }, [status]);

    async function stop() {
        if (!(await confirmDialog({ title: "Stop recording?", body: "The session is saved and can be downloaded afterwards.", confirm: "Stop", danger: true }))) return;
        if (await run(() => post("/api/session/stop"), "Recording stopped")) { await refresh(); loadSessions(); }
    }

    async function remove(s) {
        if (!(await confirmDialog({ title: `Delete “${s.name}”?`, body: `All ${s.count.toLocaleString()} readings will be deleted. This cannot be undone.`, confirm: "Delete", danger: true }))) return;
        if (await run(() => post(`/api/session/delete?id=${s.id}`), "Session deleted")) {
            if (view && view.id === s.id) { setView(null); setRows([]); cursor.current = { id: 0, next: 0, step: 1, rows: [] }; }
            loadSessions(); refresh();
        }
    }

    const r = status && status.reading;
    const x = status && status.active ? status.session : null;
    const errs = rows.filter((row) => row[2] === null).length;

    return (
        <>
            <section class="card">
                <h2>Now</h2>
                <div class="row">
                    <div class="reading"><div class="label">Temperature</div><div class="big">{r && r.ok && r.temperature !== undefined ? r.temperature : "--"}<small> °C</small></div></div>
                    <div class="reading"><div class="label">Humidity</div><div class="big">{r && r.ok && r.humidity !== undefined ? r.humidity : "--"}<small> %</small></div></div>
                    <div class="reading">
                        <div class="label">Clock</div>
                        <div style="font-size:18px;margin-top:8px">{status && status.time.valid ? new Date(status.time.epoch * 1000).toLocaleString() : "not set"}</div>
                        <div class="label">{status ? (status.time.source === "ntp" ? "internet time (NTP)" : status.time.source === "browser" ? "set from a browser" : "waiting for time") : ""}</div>
                    </div>
                </div>
                {r && !r.ok && <p class="err-text">The sensor is not responding. Check the wiring and the pull-up resistor.</p>}
            </section>

            <section class="card">
                <h2>Experiment</h2>
                {x ? (
                    <>
                        <div style="font-size:22px;font-weight:650">{x.name}</div>
                        <div class="muted">
                            Recording for {duration(x.elapsed)} · {x.count.toLocaleString()} readings every {x.intervalMs / 1000} s
                            {x.errors ? ` · ${x.errors} sensor errors` : ""}{x.resumes ? ` · resumed after ${x.resumes} restart(s)` : ""}
                            {x.startEpoch ? ` · started ${dateTime(x.startEpoch)}` : ""}
                        </div>
                        {x.notes && <p class="muted" style="margin:8px 0 0">{x.notes}</p>}
                        <Stats s={x} />
                        <div class="actions">
                            <button class="danger" onClick={stop}>Stop recording</button>
                            <a class="btn secondary" href={`/api/session/csv?id=${x.id}`}>Download CSV so far</a>
                        </div>
                    </>
                ) : (
                    <StartForm status={status} onStarted={async () => { setView(null); await refresh(); loadSessions(); }} />
                )}
            </section>

            <section class="card">
                <h2>{view ? `Chart · ${view.name}` : "Chart"}</h2>
                <div class="legend"><span><i style="background:var(--temp)" />Temperature (°C)</span><span><i style="background:var(--hum)" />Humidity (%)</span></div>
                <Chart rows={rows} emptyText={view ? "No readings yet" : "Start a session, or pick one below, to see its chart"} />
                {rows.length > 0 && (
                    <p class="help">
                        {rows.length.toLocaleString()} points{cursor.current.step > 1 ? ` (every ${cursor.current.step}th reading)` : ""}
                        {errs ? ` · ${errs} sensor errors shown as gaps` : ""} · time since start
                    </p>
                )}
            </section>

            <section class="card">
                <h2>Sessions</h2>
                {sessions === null ? <div class="empty">Loading…</div> : !sessions.length ? (
                    <div class="empty">No sessions yet. Start one above.</div>
                ) : (
                    <div class="tablewrap">
                        <table>
                            <thead><tr><th>#</th><th>Name</th><th>Started</th><th>Duration</th><th class="num">Readings</th><th>Temp °C min / avg / max</th><th>Hum % min / avg / max</th><th /></tr></thead>
                            <tbody>
                                {sessions.map((s) => (
                                    <tr key={s.id}>
                                        <td>{s.id}</td>
                                        <td title={s.notes}>{s.name} {s.active && <span class="badge ok">recording</span>}{s.stopReason === "storage full" && <span class="badge bad">storage full</span>}</td>
                                        <td>{dateTime(s.startEpoch)}</td>
                                        <td>{duration(sessionDuration(s, status))}</td>
                                        <td class="num">{s.count.toLocaleString()}</td>
                                        <td>{s.temp ? `${s.temp.min} / ${(+s.temp.avg).toFixed(1)} / ${s.temp.max}` : "–"}</td>
                                        <td>{s.hum ? `${s.hum.min} / ${(+s.hum.avg).toFixed(1)} / ${s.hum.max}` : "–"}</td>
                                        <td>
                                            <div class="row" style="gap:6px;flex-wrap:nowrap">
                                                <button class="secondary small" onClick={() => { show(s); }} aria-pressed={view && view.id === s.id}>Chart</button>
                                                <a class="btn secondary small" href={`/api/session/csv?id=${s.id}`}>CSV</a>
                                                {!s.active && <button class="danger secondary small" onClick={() => remove(s)}>Delete</button>}
                                            </div>
                                        </td>
                                    </tr>
                                ))}
                            </tbody>
                        </table>
                    </div>
                )}
            </section>
        </>
    );
}
