import { useEffect, useRef, useState } from "preact/hooks";

// ---- Toast messages ------------------------------------------------------

let toastListener = null;
let toastId = 0;

export function toast(text, error = false) {
    toastListener && toastListener({ id: ++toastId, text, error });
}

export function Toasts() {
    const [items, setItems] = useState([]);
    useEffect(() => {
        toastListener = (t) => {
            setItems((list) => [...list.slice(-2), t]);
            setTimeout(() => setItems((list) => list.filter((x) => x.id !== t.id)), t.error ? 6000 : 3500);
        };
        return () => (toastListener = null);
    }, []);
    return (
        <div class="toasts" role="status" aria-live="polite">
            {items.map((t) => <div key={t.id} class={"toast" + (t.error ? " err" : "")}>{t.text}</div>)}
        </div>
    );
}

// ---- Confirmation dialog -------------------------------------------------

let dialogListener = null;

// Resolves true when confirmed.
export function confirmDialog(opts) {
    return new Promise((resolve) => dialogListener ? dialogListener({ ...opts, resolve }) : resolve(window.confirm(opts.title)));
}

export function Dialogs() {
    const [d, setD] = useState(null);
    const okRef = useRef(null);
    useEffect(() => {
        dialogListener = setD;
        return () => (dialogListener = null);
    }, []);
    useEffect(() => {
        if (!d) return;
        okRef.current && okRef.current.focus();
        const onKey = (e) => e.key === "Escape" && close(false);
        addEventListener("keydown", onKey);
        return () => removeEventListener("keydown", onKey);
    }, [d]);
    function close(result) {
        d.resolve(result);
        setD(null);
    }
    if (!d) return null;
    return (
        <div class="modal-bg" onClick={(e) => e.target === e.currentTarget && close(false)}>
            <div class="modal" role="dialog" aria-modal="true" aria-labelledby="dlg-title">
                <h3 id="dlg-title">{d.title}</h3>
                {d.body && <p>{d.body}</p>}
                <div class="actions">
                    <button class="ghost" onClick={() => close(false)}>Cancel</button>
                    <button ref={okRef} class={d.danger ? "danger" : ""} onClick={() => close(true)}>{d.confirm || "OK"}</button>
                </div>
            </div>
        </div>
    );
}

// ---- Form pieces ---------------------------------------------------------

export function Field({ label, help, error, children, narrow, id }) {
    return (
        <div class={"field" + (narrow ? " narrow" : "")}>
            <label class="l" for={id}>{label}</label>
            {children}
            {error ? <div class="err-text">{error}</div> : help ? <div class="help">{help}</div> : null}
        </div>
    );
}

export function PasswordInput({ id, value, onInput, placeholder, autocomplete = "new-password", invalid }) {
    const [show, setShow] = useState(false);
    return (
        <div class="pw">
            <input id={id} type={show ? "text" : "password"} value={value} placeholder={placeholder}
                autocomplete={autocomplete} autocapitalize="none" spellcheck={false} aria-invalid={invalid ? "true" : undefined}
                onInput={(e) => onInput(e.currentTarget.value)} />
            <button type="button" class="ghost small" onClick={() => setShow(!show)} aria-label={show ? "Hide password" : "Show password"}>
                {show ? "Hide" : "Show"}
            </button>
        </div>
    );
}

export const Spinner = () => <span class="spin" aria-hidden="true" />;

// Button that shows a spinner while its async handler runs.
export function AsyncButton({ onClick, children, ...rest }) {
    const [busy, setBusy] = useState(false);
    return (
        <button {...rest} disabled={busy || rest.disabled} onClick={async (e) => {
            setBusy(true);
            try { await onClick(e); } finally { setBusy(false); }
        }}>
            {busy && <Spinner />}{children}
        </button>
    );
}

// Runs an API action and reports the result as a toast.
export async function run(action, successText) {
    try {
        const r = await action();
        if (successText) toast(successText);
        return r == null || r === "" ? true : r; // some endpoints answer with an empty body
    } catch (e) {
        toast(e.message, true);
        return false;
    }
}

// Calls fn now and then every `ms` while the page is visible.
export function usePoll(fn, ms, deps = []) {
    const saved = useRef(fn);
    saved.current = fn;
    useEffect(() => {
        let timer, stopped = false;
        const tick = async () => {
            if (!document.hidden) await saved.current();
            if (!stopped) timer = setTimeout(tick, ms);
        };
        tick();
        const onVis = () => !document.hidden && saved.current();
        document.addEventListener("visibilitychange", onVis);
        return () => { stopped = true; clearTimeout(timer); document.removeEventListener("visibilitychange", onVis); };
    }, [ms, ...deps]);
}
