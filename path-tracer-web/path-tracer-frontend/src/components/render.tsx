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

    useEffect(() => {
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
            },
        });

        return () => controller.abort();
    }, [renderRequest]);

    return (
        <div className="flex items-center justify-center min-h-screen bg-gray-950">
            <canvas
                ref={canvasRef}
                width={renderRequest.X}
                height={renderRequest.Y}
                className="rounded shadow-2xl"
            />
        </div>
    );
}