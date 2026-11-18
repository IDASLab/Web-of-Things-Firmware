// Small fetch wrapper. The device answers errors as {"status":"failed","message":"..."}.

async function request(path, opts = {}) {
    let res;
    try {
        res = await fetch(path, opts);
    } catch {
        throw new Error("Device not reachable. Check the WiFi connection.");
    }
    const type = res.headers.get("content-type") || "";
    const body = type.includes("json") ? await res.json().catch(() => null) : await res.text();
    if (!res.ok) {
        const msg = body && body.message ? body.message : res.status === 401 ? "Login required" : `Request failed (HTTP ${res.status})`;
        throw new Error(msg);
    }
    return body;
}

export const get = (path) => request(path);

export const post = (path, data) =>
    request(path, data === undefined
        ? { method: "POST" }
        : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(data) });

// Upload with progress (fetch has no upload progress).
export function upload(file, onProgress) {
    return new Promise((resolve, reject) => {
        const form = new FormData();
        form.append("file", file, file.name);
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/upload");
        xhr.upload.onprogress = (e) => e.lengthComputable && onProgress && onProgress(e.loaded / e.total);
        xhr.onload = () => {
            let ok = false;
            try { ok = JSON.parse(xhr.responseText).success === true; } catch { /* not JSON */ }
            ok ? resolve() : reject(new Error("Upload failed. The file may be too large, or its name invalid (letters, digits, . _ - only, max 31 characters)."));
        };
        xhr.onerror = () => reject(new Error("Upload failed: device not reachable"));
        xhr.send(form);
    });
}
