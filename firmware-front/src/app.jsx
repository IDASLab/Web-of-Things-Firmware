import { useEffect, useRef, useState } from "preact/hooks";
import { get, post } from "./api.js";
import { Dialogs, Toasts, usePoll } from "./components/ui.jsx";
import { LoggerPage } from "./pages/Logger.jsx";
import { NetworkPage } from "./pages/Network.jsx";
import { SettingsPage } from "./pages/Settings.jsx";
import { SystemPage } from "./pages/System.jsx";

const PAGES = [
    { path: "/", title: "Logger", Page: LoggerPage },
    { path: "/network", title: "Network", Page: NetworkPage },
    { path: "/settings", title: "Settings", Page: SettingsPage },
    { path: "/system", title: "System", Page: SystemPage },
];

// Old links (/config, /network, /setting) from the previous interface
const ALIASES = { "/config": "/settings", "/setting": "/system", "/network": "/network" };

// Hash routing (#/network) works on any server path, including the captive portal.
function useRoute() {
    const read = () => {
        const p = (location.hash.replace(/^#/, "") || ALIASES[location.pathname] || "/").split("?")[0];
        return ALIASES[p] || p;
    };
    const [route, setRoute] = useState(read);
    useEffect(() => {
        const on = () => setRoute(read());
        addEventListener("hashchange", on);
        return () => removeEventListener("hashchange", on);
    }, []);
    return route;
}

function Badge({ cls, children, title }) {
    return <span class={"badge " + cls} title={title}>{children}</span>;
}

function StatusBadges({ s }) {
    if (!s) return null;
    const freePct = s.storage.total ? s.storage.free / s.storage.total : 1;
    return (
        <div class="badges">
            {s.active && <Badge cls="ok">● Recording</Badge>}
            <Badge cls={s.reading.ok ? "ok" : "bad"}>{s.sensor} {s.reading.ok ? "OK" : "error"}</Badge>
            <Badge cls={s.network ? "ok" : "warn"} title={s.network ? "Connected to WiFi" : "Not connected to a WiFi network"}>{s.network ? "WiFi" : "Offline"}</Badge>
            <Badge cls={s.time.source === "ntp" ? "ok" : s.time.valid ? "warn" : "bad"} title="Where the device clock comes from">Clock: {s.time.valid ? (s.time.source === "ntp" ? "NTP" : s.time.source) : "not set"}</Badge>
            <Badge cls={freePct < 0.1 ? "bad" : freePct < 0.25 ? "warn" : "ok"}>Storage {Math.round((1 - freePct) * 100)}%</Badge>
        </div>
    );
}

export function App() {
    const route = useRoute();
    const [status, setStatus] = useState(null);
    const [offline, setOffline] = useState(false);
    const clockSynced = useRef(false);

    async function refresh() {
        try {
            const s = await get("/api/logger/status");
            setStatus(s);
            setOffline(false);
            // No internet time: give the device this browser's clock and UTC offset.
            if (!clockSynced.current && (!s.time.valid || s.time.source !== "ntp")) {
                clockSynced.current = true;
                post("/api/time/set", { epoch: Math.floor(Date.now() / 1000), offset: -new Date().getTimezoneOffset() }).catch(() => {});
            }
        } catch {
            setOffline(true);
        }
    }

    const interval = Math.max(3000, Math.min(10000, status ? status.sampleRate : 5000));
    usePoll(refresh, interval, [interval]);

    const page = PAGES.find((p) => p.path === route) || PAGES[0];
    useEffect(() => {
        document.title = `${page.title} · ${status ? status.deviceName : "Temperature Logger"}`;
    }, [page, status && status.deviceName]);

    return (
        <>
            <header class="top">
                <div class="top-inner">
                    <div class="brand">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="var(--temp)" stroke-width="2" aria-hidden="true"><path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z" /></svg>
                        <span>{status ? status.deviceName : "Temperature Logger"}</span>
                    </div>
                    <nav class="tabs" aria-label="Pages">
                        {PAGES.map((p) => (
                            <a key={p.path} href={"#" + p.path} class={p === page ? "active" : ""} aria-current={p === page ? "page" : undefined}>{p.title}</a>
                        ))}
                    </nav>
                    <StatusBadges s={status} />
                </div>
            </header>
            <main>
                {offline && <div class="banner" role="alert">Can't reach the device. Retrying… Check that you are on the same WiFi network or on the device's hotspot.</div>}
                <page.Page status={status} refresh={refresh} />
            </main>
            <Toasts />
            <Dialogs />
        </>
    );
}
