import { defineConfig } from "vite";
import preact from "@preact/preset-vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// During development, API calls are proxied to a real device:
//   DEVICE=http://192.168.1.50 npm run dev
// The proxy drops the Origin and Referer headers so the device's
// cross-site check accepts the forwarded requests; no CORS build is needed.
const device = process.env.DEVICE || "http://192.168.4.1";
const target = {
    target: device,
    changeOrigin: true,
    configure: (proxy) => proxy.on("proxyReq", (req) => {
        req.removeHeader("origin");
        req.removeHeader("referer");
    }),
};

export default defineConfig({
    plugins: [preact(), viteSingleFile()],
    build: {
        target: "es2019",
        assetsInlineLimit: 100000000,
        cssCodeSplit: false,
        reportCompressedSize: false,
    },
    server: {
        proxy: {
            "/api": target,
            "/upload": target,
            "/download": target,
        },
    },
});
