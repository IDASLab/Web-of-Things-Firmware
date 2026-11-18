import { useEffect, useState } from "preact/hooks";
import { get, post } from "../api.js";
import { AsyncButton, Field, PasswordInput, run, toast } from "../components/ui.jsx";
import { isIP } from "../util.js";

const TZ_PRESETS = [
    ["", "Automatic (from this browser, no daylight saving)"],
    ["UTC0", "UTC"],
    ["GMT0BST,M3.5.0/1,M10.5.0", "UK / Ireland"],
    ["CET-1CEST,M3.5.0,M10.5.0/3", "Central Europe"],
    ["EET-2EEST,M3.5.0/3,M10.5.0/4", "Eastern Europe"],
    ["<+0330>-03:30", "Iran (UTC+03:30)"],
    ["<+04>-4", "Gulf (UTC+04)"],
    ["IST-5:30", "India (UTC+05:30)"],
    ["CST-8", "China (UTC+08)"],
    ["JST-9", "Japan (UTC+09)"],
    ["AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney"],
    ["EST5EDT,M3.2.0,M11.1.0", "US Eastern"],
    ["CST6CDT,M3.2.0,M11.1.0", "US Central"],
    ["MST7MDT,M3.2.0,M11.1.0", "US Mountain"],
    ["PST8PDT,M3.2.0,M11.1.0", "US Pacific"],
];

function Card({ title, hint, children }) {
    return <section class="card"><h2>{title}</h2>{hint && <p class="hint">{hint}</p>}{children}</section>;
}

function DeviceCard({ cfg, reload }) {
    const [f, setF] = useState({ name: cfg.DeviceName, ssid: cfg.DeviceSSID, pass: "", custom: false, ip: cfg.DeviceIP, subnet: cfg.DeviceSub, gateway: cfg.DeviceGw });
    const set = (k) => (v) => setF({ ...f, [k]: typeof v === "string" ? v : v.currentTarget.value });
    const errors = {
        name: !f.name.trim() ? "Required" : null,
        ssid: f.ssid.length > 32 ? "Max 32 characters" : null,
        pass: f.pass && (f.pass.length < 8 || f.pass.length > 63) ? "8 to 63 characters" : null,
    };
    if (f.custom) for (const k of ["ip", "subnet", "gateway"]) if (!isIP(f[k])) errors[k] = "Invalid address";
    const valid = Object.values(errors).every((e) => !e);

    async function save() {
        if (!valid) return;
        const body = { name: f.name.trim(), ssid: f.ssid.trim(), pass: f.pass };
        const ok = f.custom
            ? await run(() => post("/api/config/device/setStatic", { ...body, ip: f.ip.trim(), subnet: f.subnet.trim(), gateway: f.gateway.trim() }), "Device settings saved. Hotspot changes apply after a restart.")
            : await run(() => post("/api/config/device/set", body), "Device settings saved. Hotspot changes apply after a restart.");
        if (ok) { setF({ ...f, pass: "" }); reload(); }
    }

    return (
        <Card title="Device" hint="The hotspot opens when the device can't join a WiFi network.">
            <div class="row">
                <Field id="dname" label="Device name" error={errors.name}>
                    <input id="dname" maxLength={32} value={f.name} onInput={set("name")} aria-invalid={errors.name ? "true" : undefined} />
                </Field>
            </div>
            <div class="row" style="margin-top:12px">
                <Field id="dssid" label="Hotspot name" help="Empty = WoT-<chip id>" error={errors.ssid}>
                    <input id="dssid" maxLength={32} value={f.ssid} onInput={set("ssid")} autocapitalize="none" />
                </Field>
                <Field id="dpass" label="Hotspot password" help="Leave empty to keep the current one" error={errors.pass}>
                    <PasswordInput id="dpass" value={f.pass} onInput={set("pass")} placeholder="unchanged" invalid={errors.pass} />
                </Field>
            </div>
            <div style="margin-top:12px">
                <label class="check"><input type="checkbox" checked={f.custom} onChange={(e) => setF({ ...f, custom: e.currentTarget.checked })} /> Custom hotspot address</label>
            </div>
            {f.custom && (
                <div class="row" style="margin-top:12px">
                    {[["ip", "Hotspot IP"], ["subnet", "Subnet mask"], ["gateway", "Gateway"]].map(([k, l]) => (
                        <Field key={k} id={"d" + k} label={l} error={errors[k]}>
                            <input id={"d" + k} inputMode="decimal" value={f[k]} onInput={set(k)} aria-invalid={errors[k] ? "true" : undefined} />
                        </Field>
                    ))}
                </div>
            )}
            <div class="actions"><AsyncButton onClick={save} disabled={!valid}>Save</AsyncButton></div>
        </Card>
    );
}

function SensorCard({ cfg, status, reload }) {
    const [rate, setRate] = useState(String(cfg.SampelingRate / 1000));
    const [toff, setToff] = useState(String(status.tempOffset));
    const [hoff, setHoff] = useState(String(status.humOffset));
    const errors = {
        rate: !(Number(rate) >= 2 && Number(rate) <= 3600) ? "2 to 3600 seconds" : null,
        toff: !(Math.abs(Number(toff)) <= 10) || toff === "" ? "-10 to 10 °C" : null,
        hoff: !(Math.abs(Number(hoff)) <= 20) || hoff === "" ? "-20 to 20 %RH" : null,
    };
    const valid = Object.values(errors).every((e) => !e);

    async function save() {
        if (!valid) return;
        const a = await run(() => post("/api/config/sensor/set", { rate: Math.round(Number(rate) * 1000) }));
        const b = a && await run(() => post("/api/config/calibration/set", { temp: Number(toff), hum: Number(hoff) }));
        if (b) { toast("Sensor settings saved"); reload(); }
    }

    return (
        <Card title="Sensor and calibration" hint={`${status.sensor} on GPIO ${cfg.SensorGPIO}. Offsets are added to each reading; compare with a reference thermometer to find them.`}>
            <div class="row">
                <Field id="rate" label="Sample interval (seconds)" narrow error={errors.rate}>
                    <input id="rate" type="number" min="2" max="3600" step="1" value={rate} onInput={(e) => setRate(e.currentTarget.value)} aria-invalid={errors.rate ? "true" : undefined} />
                </Field>
                <Field id="toff" label="Temperature offset (°C)" narrow error={errors.toff}>
                    <input id="toff" type="number" min="-10" max="10" step="0.1" value={toff} onInput={(e) => setToff(e.currentTarget.value)} aria-invalid={errors.toff ? "true" : undefined} />
                </Field>
                <Field id="hoff" label="Humidity offset (%RH)" narrow error={errors.hoff}>
                    <input id="hoff" type="number" min="-20" max="20" step="0.1" value={hoff} onInput={(e) => setHoff(e.currentTarget.value)} aria-invalid={errors.hoff ? "true" : undefined} />
                </Field>
            </div>
            {status.active && <p class="help">A session is recording. New settings apply to readings from now on.</p>}
            <div class="actions"><AsyncButton onClick={save} disabled={!valid}>Save</AsyncButton></div>
        </Card>
    );
}

function TimeCard({ status, refresh }) {
    const preset = TZ_PRESETS.some(([v]) => v === status.time.tz);
    const [ntp, setNtp] = useState(status.time.ntp);
    const [tzSel, setTzSel] = useState(preset ? status.time.tz : "custom");
    const [tz, setTz] = useState(status.time.tz);
    const tzValue = tzSel === "custom" ? tz.trim() : tzSel;
    const tzError = /\s/.test(tzValue) ? "No spaces allowed" : tzValue.length > 48 ? "Max 48 characters" : null;

    async function save() {
        if (tzError) return;
        if (await run(() => post("/api/config/time/set", { ntp: ntp.trim(), tz: tzValue }), "Time settings saved")) {
            if (!tzValue) await post("/api/time/set", { epoch: Math.floor(Date.now() / 1000), offset: -new Date().getTimezoneOffset() }).catch(() => {});
            refresh();
        }
    }
    async function sync() {
        if (await run(() => post("/api/time/set", { epoch: Math.floor(Date.now() / 1000), offset: -new Date().getTimezoneOffset() }), "Clock set from this browser")) refresh();
    }

    return (
        <Card title="Time" hint={status.time.valid ? `Device time: ${new Date(status.time.epoch * 1000).toLocaleString()} (${status.time.source === "ntp" ? "internet time" : "set from a browser"})` : "The device clock is not set."}>
            <div class="row">
                <Field id="ntp" label="NTP server" help="Used when the device has internet. Empty = off.">
                    <input id="ntp" maxLength={64} value={ntp} onInput={(e) => setNtp(e.currentTarget.value)} autocapitalize="none" />
                </Field>
                <Field id="tzsel" label="Time zone">
                    <select id="tzsel" value={tzSel} onChange={(e) => setTzSel(e.currentTarget.value)}>
                        {TZ_PRESETS.map(([v, l]) => <option key={v} value={v}>{l}</option>)}
                        <option value="custom">Custom POSIX TZ string…</option>
                    </select>
                </Field>
            </div>
            {tzSel === "custom" && (
                <div class="row" style="margin-top:12px">
                    <Field id="tz" label="POSIX TZ" help="e.g. CET-1CEST,M3.5.0,M10.5.0/3" error={tzError}>
                        <input id="tz" maxLength={48} value={tz} onInput={(e) => setTz(e.currentTarget.value)} autocapitalize="none" spellcheck={false} />
                    </Field>
                </div>
            )}
            <div class="actions">
                <AsyncButton onClick={save} disabled={!!tzError}>Save</AsyncButton>
                <AsyncButton class="secondary" onClick={sync}>Set clock from this browser</AsyncButton>
            </div>
        </Card>
    );
}

function MqttCard({ cfg, reload }) {
    const [f, setF] = useState({ on: !!cfg.MQTTStatus, host: cfg.MQTTBrokerIP || "", port: String(cfg.MQTTBrokerPort || 1883), topic: cfg.MQTTBrokerTopic || "/temperature", qos: String(cfg.MQTTBrokerQoS || 0) });
    const set = (k) => (e) => setF({ ...f, [k]: e.currentTarget.value });
    const errors = f.on ? {
        host: !f.host.trim() ? "Required" : f.host.length > 64 ? "Max 64 characters" : null,
        port: !(Number(f.port) >= 1 && Number(f.port) <= 65535 && Number.isInteger(Number(f.port))) ? "1 to 65535" : null,
        topic: !f.topic.trim() ? "Required" : f.topic.length > 64 ? "Max 64 characters" : null,
    } : {};
    const valid = Object.values(errors).every((e) => !e);

    async function save() {
        if (!valid) return;
        if (await run(() => post("/api/config/broker/set", { status: f.on, ip: f.host.trim(), port: Number(f.port), topic: f.topic.trim(), qos: Number(f.qos) }), "MQTT settings saved")) reload();
    }

    return (
        <Card title="MQTT (optional)" hint="Publishes every valid reading as JSON to a broker on your network.">
            <label class="check"><input type="checkbox" checked={f.on} onChange={(e) => setF({ ...f, on: e.currentTarget.checked })} /> Publish readings to MQTT</label>
            <div class="row" style="margin-top:12px">
                <Field id="mhost" label="Broker address" help="IP address or hostname" error={errors.host}>
                    <input id="mhost" value={f.host} disabled={!f.on} onInput={set("host")} autocapitalize="none" spellcheck={false} />
                </Field>
                <Field id="mport" label="Port" narrow error={errors.port}>
                    <input id="mport" type="number" min="1" max="65535" value={f.port} disabled={!f.on} onInput={set("port")} />
                </Field>
            </div>
            <div class="row" style="margin-top:12px">
                <Field id="mtopic" label="Topic" error={errors.topic}>
                    <input id="mtopic" value={f.topic} disabled={!f.on} onInput={set("topic")} autocapitalize="none" spellcheck={false} />
                </Field>
                <Field id="mqos" label="QoS" narrow>
                    <select id="mqos" value={f.qos} disabled={!f.on} onChange={set("qos")}>
                        <option value="0">0 · at most once</option><option value="1">1 · at least once</option><option value="2">2 · exactly once</option>
                    </select>
                </Field>
            </div>
            <div class="actions"><AsyncButton onClick={save} disabled={!valid}>Save</AsyncButton></div>
        </Card>
    );
}

function AdminCard() {
    const [cur, setCur] = useState("");
    const [pw, setPw] = useState("");
    const [pw2, setPw2] = useState("");
    const errors = {
        pw: pw && (pw.length < 8 || pw.length > 64) ? "8 to 64 characters" : null,
        pw2: pw2 && pw2 !== pw ? "Passwords don't match" : null,
    };
    const valid = cur && pw && pw2 && !errors.pw && !errors.pw2;

    async function save() {
        if (!valid) return;
        if (await run(() => post("/api/config/admin/set", { current: cur, password: pw }))) {
            setCur(""); setPw(""); setPw2("");
            toast("Password changed. Your browser will ask you to log in again with the new password.");
        }
    }

    return (
        <Card title="Admin login" hint="User name: admin. Protects every page and setting.">
            <div class="row">
                <Field id="acur" label="Current password"><PasswordInput id="acur" value={cur} onInput={setCur} autocomplete="current-password" /></Field>
                <Field id="anew" label="New password" error={errors.pw}><PasswordInput id="anew" value={pw} onInput={setPw} invalid={errors.pw} /></Field>
                <Field id="anew2" label="Repeat new password" error={errors.pw2}><PasswordInput id="anew2" value={pw2} onInput={setPw2} invalid={errors.pw2} /></Field>
            </div>
            <div class="actions"><AsyncButton onClick={save} disabled={!valid}>Change password</AsyncButton></div>
        </Card>
    );
}

export function SettingsPage({ status, refresh }) {
    const [cfg, setCfg] = useState(null);
    const [version, setVersion] = useState(0);

    async function load() {
        try {
            setCfg(await get("/api/config/get"));
            setVersion((v) => v + 1);
        } catch (e) {
            toast(e.message, true);
        }
    }
    useEffect(() => { load(); }, []);

    if (!cfg || !status) return <section class="card"><div class="empty">Loading settings…</div></section>;

    const reload = () => { load(); refresh(); };
    return (
        <div class="grid2" key={version}>
            <SensorCard cfg={cfg} status={status} reload={reload} />
            <TimeCard status={status} refresh={refresh} />
            <DeviceCard cfg={cfg} reload={reload} />
            <MqttCard cfg={cfg} reload={reload} />
            <AdminCard />
        </div>
    );
}
