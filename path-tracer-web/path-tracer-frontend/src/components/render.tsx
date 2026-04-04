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
                const pixel: Pixel = JSON.parse(wrapper.message);
                const { x: r, y: g, z: b } = pixel.color;
                const R = Math.round(r * 255);
                const G = Math.round(g * 255);
                const B = Math.round(b * 255);
                ctx.fillStyle = `rgba(${R}, ${G}, ${B}, ${pixel.alpha})`;
                ctx.fillRect(pixel.X, pixel.Y, 1, 1);
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