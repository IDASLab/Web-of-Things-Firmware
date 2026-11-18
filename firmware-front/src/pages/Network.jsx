import { useEffect, useState } from "preact/hooks";
import { get, post } from "../api.js";
import { AsyncButton, Field, PasswordInput, confirmDialog, run, toast } from "../components/ui.jsx";
import { isIP } from "../util.js";

export function NetworkPage({ status }) {
    const [info, setInfo] = useState(null);
    const [portal, setPortal] = useState(null);
    const [ssid, setSsid] = useState("");
    const [pass, setPass] = useState("");
    const [dhcp, setDhcp] = useState(true);
    const [ip, setIp] = useState({ ip: "", subnet: "255.255.255.0", gateway: "", dns: "8.8.8.8" });
    const [touched, setTouched] = useState(false);

    async function load() {
        try {
            const [d, p] = await Promise.all([get("/api/dashboard/get"), get("/api/network/get")]);
            setInfo(d);
            setPortal(p);
        } catch (e) {
            toast(e.message, true);
        }
    }
    useEffect(() => { load(); }, []);
    useEffect(() => { load(); }, [status && status.network]);

    const errors = {
        ssid: !ssid.trim() ? "Enter the network name" : ssid.length > 32 ? "Max 32 characters" : null,
        pass: pass.length > 0 && pass.length < 8 ? "WiFi passwords are at least 8 characters (leave empty for an open network)" : pass.length > 64 ? "Max 64 characters" : null,
    };
    if (!dhcp) for (const k of ["ip", "subnet", "gateway", "dns"]) if (!isIP(ip[k])) errors[k] = "Enter an address like 192.168.1.50";
    const valid = Object.values(errors).every((e) => !e);
    const show = (k) => (touched ? errors[k] : null);

    async function save() {
        setTouched(true);
        if (!valid) return;
        const ok = await confirmDialog({
            title: `Join “${ssid.trim()}”?`,
            body: "The device will try this network for up to 20 seconds. If it fails, it goes back to the previous network or its hotspot. You may need to reconnect to the device at its new address.",
            confirm: "Join network",
        });
        if (!ok) return;
        const body = { ssid: ssid.trim(), pass };
        const done = dhcp
            ? await run(() => post("/api/network/set", body))
            : await run(() => post("/api/network/setStaticIP", { ...body, ...Object.fromEntries(Object.entries(ip).map(([k, v]) => [k, v.trim()])) }));
        if (done) {
            toast(`Connecting to ${body.ssid}… Check this page again in about 30 seconds.`);
            setPass("");
            setTouched(false);
            setTimeout(load, 25000);
        }
    }

    async function forget() {
        const ok = await confirmDialog({
            title: "Forget this WiFi network?",
            body: `The device disconnects and opens its own hotspot “${portal ? portal.ssid : "WoT-…"}”. Connect to that hotspot to reach it again.`,
            confirm: "Forget network",
            danger: true,
        });
        if (ok) await run(() => post("/api/network/forget"), "Network forgotten. The device's hotspot is starting.");
    }

    const connected = status ? status.network : null;

    return (
        <div class="grid2">
            <section class="card">
                <h2>Connection</h2>
                {!info ? <div class="empty">Loading…</div> : (
                    <dl class="kv">
                        <dt>WiFi</dt>
                        <dd>{connected ? <span class="badge ok">Connected</span> : <span class="badge warn">Not connected</span>}</dd>
                        {connected && <><dt>Network</dt><dd>{info.networkSSID}</dd></>}
                        {connected && <><dt>Address</dt><dd><a href={`http://${info.networkAddress}/`}>{info.networkAddress}</a></dd></>}
                        <dt>Hotspot</dt>
                        <dd>{portal && portal.status ? <>On: <b>{portal.ssid}</b></> : "Off (opens when WiFi is unavailable)"}</dd>
                        {portal && portal.status && !connected && <><dt>Address</dt><dd>{info.networkAddress}</dd></>}
                    </dl>
                )}
                {connected && (
                    <div class="actions"><button class="danger secondary" onClick={forget}>Forget network</button></div>
                )}
            </section>

            <section class="card">
                <h2>Join a WiFi network</h2>
                <div class="row">
                    <Field id="ssid" label="Network name (SSID)" error={show("ssid")}>
                        <input id="ssid" value={ssid} maxLength={32} autocapitalize="none" spellcheck={false} aria-invalid={show("ssid") ? "true" : undefined}
                            onInput={(e) => setSsid(e.currentTarget.value)} />
                    </Field>
                    <Field id="wpass" label="Password" error={show("pass")}>
                        <PasswordInput id="wpass" value={pass} onInput={setPass} invalid={show("pass")} />
                    </Field>
                </div>
                <div style="margin-top:12px">
                    <label class="check"><input type="checkbox" checked={dhcp} onChange={(e) => setDhcp(e.currentTarget.checked)} /> Get an address automatically (DHCP)</label>
                </div>
                {!dhcp && (
                    <div class="row" style="margin-top:12px">
                        {[["ip", "IP address"], ["subnet", "Subnet mask"], ["gateway", "Gateway"], ["dns", "DNS server"]].map(([k, label]) => (
                            <Field key={k} id={"n" + k} label={label} error={show(k)}>
                                <input id={"n" + k} inputMode="decimal" value={ip[k]} aria-invalid={show(k) ? "true" : undefined}
                                    onInput={(e) => setIp({ ...ip, [k]: e.currentTarget.value })} />
                            </Field>
                        ))}
                    </div>
                )}
                <div class="actions"><AsyncButton onClick={save}>Join network</AsyncButton></div>
            </section>
        </div>
    );
}
