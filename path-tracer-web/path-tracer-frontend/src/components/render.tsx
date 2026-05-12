import { fetchEventSource } from "@microsoft/fetch-event-source";
import type { EventSourceMessage } from "@microsoft/fetch-event-source";
import type RenderRequest from "../types";
import { useEffect, useRef } from "react";

interface Pixel {
    X: number;
    Y: number;
    color: { x: number; y: number; z: number };
    alpha: number;
}

export default function RenderPage({ renderRequest }: { renderRequest: RenderRequest }) {

    const canvasRef = useRef<HTMLCanvasElement>(null);
    const statusRef = useRef<HTMLParagraphElement>(null);
    const fetchStarted = useRef(false);

    useEffect(() => {
        // Prevent duplicate requests from React re-renders or strict mode double-mounting.
        if (fetchStarted.current) return;
        fetchStarted.current = true;

        const canvas = canvasRef.current;
        if (!canvas) return;
        const ctx = canvas.getContext("2d");
        if (!ctx) return;

        const imageData = ctx.createImageData(canvas.width, canvas.height);
        const imageData2 = ctx.createImageData(canvas.width, canvas.height);

        const controller = new AbortController();

        fetchEventSource("/api/render", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(renderRequest),
            signal: controller.signal,
            onmessage(event: EventSourceMessage) {
                if (event.event === "done") {
                    if (statusRef.current) statusRef.current.textContent = "Render complete";
                    controller.abort();
                    return;
                }
                if (event.event === "keepalive" || event.event === "status") {
                    if (event.event === "status") {
                        const wrapper = JSON.parse(event.data) as { message: string };
                        if (statusRef.current) statusRef.current.textContent = wrapper.message;
                    }
                    return;
                }
                if (event.event === "error") {
                    const wrapper = JSON.parse(event.data) as { message: string };
                    console.error("Server error:", wrapper.message);
                    if (statusRef.current) statusRef.current.textContent = "Error: " + wrapper.message;
                    return;
                }
                // renderUpdate events
                if (statusRef.current) statusRef.current.textContent = "";
                const wrapper = JSON.parse(event.data) as { message: string };
                const pixels: Pixel[] = JSON.parse(wrapper.message);
                for (const pixel of pixels) {
                    const { x: r, y: g, z: b } = pixel.color;
                    console.log(`Received pixel update: (${pixel.X}, ${pixel.Y}) = (${r}, ${g}, ${b}, ${pixel.alpha})`);
                    
                    const R = Math.min(255, Math.round(Math.pow(Math.max(0, r), 1 / 2.2) * 255));
                    const G = Math.min(255, Math.round(Math.pow(Math.max(0, g), 1 / 2.2) * 255));
                    const B = Math.min(255, Math.round(Math.pow(Math.max(0, b), 1 / 2.2) * 255));
                    const A = Math.min(255, Math.round(pixel.alpha * 255));
                    const idx = (pixel.Y * canvas.width + pixel.X) * 4;
                    imageData.data[idx] = R;
                    imageData.data[idx + 1] = G;
                    imageData.data[idx + 2] = B;
                    imageData.data[idx + 3] = A;

                    imageData2.data[idx] = r;
                    imageData2.data[idx + 1] = g;
                    imageData2.data[idx + 2] = b;
                    imageData2.data[idx + 3] = pixel.alpha;
                }
                ctx.putImageData(imageData, 0, 0);
                ctx.putImageData(imageData2, 0, 0);
            },
            onerror(err: Error) {
                console.error("SSE error:", err);
                throw err; // Stop fetchEventSource from auto-retrying
            },
            onclose() {
                throw new Error("Stream closed");
            },
            openWhenHidden: true, // Don't close/retry when tab loses focus
        });

        return () => controller.abort();
    }, [renderRequest]);

    return (
        <div className="flex items-center justify-center min-h-screen bg-white">
            <p ref={statusRef} className="absolute top-4 text-black text-sm" />
            <canvas
                ref={canvasRef}
                width={renderRequest.X}
                height={renderRequest.Y}
                className="rounded shadow-2xl"
            />
        </div>
    );
}