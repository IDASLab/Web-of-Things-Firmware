import { useEffect, useRef, useState } from "preact/hooks";
import { duration } from "../util.js";

// Temperature (left axis) and humidity (right axis) over time since start.
// rows: [elapsed_s, unix_time, temp|null, hum|null]

const H = 300, L = 44, R = 44, T = 10, B = 28;

function niceRange(lo, hi) {
    if (!isFinite(lo)) { lo = 0; hi = 1; }
    if (hi - lo < 1) { const m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }
    const pad = (hi - lo) * 0.08;
    return [lo - pad, hi + pad];
}

function ticks(lo, hi, n) {
    const raw = (hi - lo) / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const step = [1, 2, 5, 10].map((k) => k * mag).find((k) => k >= raw);
    const out = [];
    for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) out.push(+v.toFixed(6));
    return out;
}

function timeTicks(span, n) {
    const nice = [10, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400, 172800, 604800];
    const step = nice.find((k) => span / k <= n) || Math.ceil(span / n / 86400) * 86400;
    const out = [];
    for (let v = 0; v <= span; v += step) out.push(v);
    return out;
}

function path(rows, idx, X, Y) {
    // Break the line where the sensor failed
    let d = "", pen = false;
    for (const r of rows) {
        if (r[idx] === null) { pen = false; continue; }
        d += (pen ? "L" : "M") + X(r[0]).toFixed(1) + "," + Y(r[idx]).toFixed(1);
        pen = true;
    }
    return d;
}

export function Chart({ rows, emptyText }) {
    const ref = useRef(null);
    const [W, setW] = useState(600);
    const [hover, setHover] = useState(null);

    useEffect(() => {
        const el = ref.current;
        const ro = new ResizeObserver(() => setW(el.clientWidth || 600));
        ro.observe(el);
        return () => ro.disconnect();
    }, []);

    const pts = rows.filter((r) => r[2] !== null);
    if (!pts.length) {
        return <svg ref={ref} class="chart" viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Chart"><text x={W / 2} y={H / 2} text-anchor="middle">{emptyText}</text></svg>;
    }

    const x0 = rows[0][0], x1 = Math.max(rows[rows.length - 1][0], x0 + 1);
    let tMin = Infinity, tMax = -Infinity, hMin = Infinity, hMax = -Infinity;
    for (const r of pts) {
        tMin = Math.min(tMin, r[2]); tMax = Math.max(tMax, r[2]);
        hMin = Math.min(hMin, r[3]); hMax = Math.max(hMax, r[3]);
    }
    const [t0, t1] = niceRange(tMin, tMax), [h0, h1] = niceRange(hMin, hMax);
    const X = (v) => L + ((v - x0) / (x1 - x0)) * (W - L - R);
    const YT = (v) => T + ((t1 - v) / (t1 - t0)) * (H - T - B);
    const YH = (v) => T + ((h1 - v) / (h1 - h0)) * (H - T - B);

    function onMove(e) {
        const b = ref.current.getBoundingClientRect();
        const xv = x0 + ((e.clientX - b.left - (L * b.width) / W) / (b.width * (1 - (L + R) / W))) * (x1 - x0);
        let best = rows[0];
        for (const r of rows) if (Math.abs(r[0] - xv) < Math.abs(best[0] - xv)) best = r;
        setHover({ r: best, x: e.clientX, y: e.clientY });
    }

    const r = hover && hover.r;
    return (
        <>
            <svg ref={ref} class="chart" viewBox={`0 0 ${W} ${H}`} role="img"
                aria-label={`Temperature ${tMin} to ${tMax} °C and humidity ${hMin} to ${hMax} % over ${duration(x1 - x0)}`}
                onMouseMove={onMove} onMouseLeave={() => setHover(null)}
                onTouchMove={(e) => onMove(e.touches[0])} onTouchEnd={() => setHover(null)}>
                {ticks(t0, t1, 5).map((v) => (
                    <g key={"t" + v}>
                        <line x1={L} x2={W - R} y1={YT(v)} y2={YT(v)} stroke="var(--line)" />
                        <text x={L - 6} y={YT(v) + 4} text-anchor="end" style="fill:var(--temp)">{v}</text>
                    </g>
                ))}
                {ticks(h0, h1, 5).map((v) => <text key={"h" + v} x={W - R + 6} y={YH(v) + 4} style="fill:var(--hum)">{v}</text>)}
                {timeTicks(x1 - x0, Math.max(2, Math.floor(W / 110))).map((v) => (
                    <text key={"x" + v} x={X(x0 + v)} y={H - 8} text-anchor="middle">{duration(v)}</text>
                ))}
                <path d={path(rows, 3, X, YH)} fill="none" stroke="var(--hum)" stroke-width="1.5" />
                <path d={path(rows, 2, X, YT)} fill="none" stroke="var(--temp)" stroke-width="2" />
                {r && <line x1={X(r[0])} x2={X(r[0])} y1={T} y2={H - B} stroke="var(--muted)" stroke-dasharray="3 3" />}
                {r && r[2] !== null && <circle cx={X(r[0])} cy={YT(r[2])} r="3.5" fill="var(--temp)" />}
                {r && r[3] !== null && <circle cx={X(r[0])} cy={YH(r[3])} r="3.5" fill="var(--hum)" />}
            </svg>
            {r && (
                <div class="tip" style={{ left: Math.min(hover.x + 12, innerWidth - 190) + "px", top: hover.y + 12 + "px" }}>
                    {r[1] ? new Date(r[1] * 1000).toLocaleString() : "+" + duration(r[0])}<br />
                    <b style="color:var(--temp)">{r[2] === null ? "sensor error" : r[2] + " °C"}</b> · <b style="color:var(--hum)">{r[3] === null ? "–" : r[3] + " %"}</b>
                </div>
            )}
        </>
    );
}
