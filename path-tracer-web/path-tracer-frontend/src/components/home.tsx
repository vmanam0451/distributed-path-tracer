
import { useState } from "react";
import { useNavigate } from "react-router";
import type RenderRequest from "../types";

function Field({
    label,
    name,
    type = "text",
    value,
    onChange,
}: {
    label: string;
    name: string;
    type?: string;
    value: string | number;
    onChange: (e: React.ChangeEvent<HTMLInputElement>) => void;
}) {
    return (
        <div className="flex flex-col gap-1">
            <label className="text-xs font-medium text-gray-400" htmlFor={name}>
                {label}
            </label>
            <input
                id={name}
                name={name}
                type={type}
                value={value}
                onChange={onChange}
                className="rounded-md border border-gray-700 bg-gray-800 px-3 py-2 text-sm text-gray-100 placeholder:text-gray-600 focus:outline-none focus:ring-2 focus:ring-green-500 focus:border-transparent"
            />
        </div>
    );
}

export default function HomePage({setRenderRequest}: {setRenderRequest: (renderRequest: RenderRequest) => void}) {

    const navigate = useNavigate();

    const [form, setForm] = useState<RenderRequest>({
        sceneBucket: "",
        sceneKey: "",
        sceneName: "",
        numWorkers: 4,
        numSamples: 64,
        numBounces: 8,
        X: 800,
        Y: 600,
    });

    function handleChange(e: React.ChangeEvent<HTMLInputElement>) {
        const { name, value, type } = e.target;
        setForm(prev => ({
            ...prev,
            [name]: type === "number" ? Number(value) : value,
        }));
    }

    function handleSubmit(e: React.FormEvent) {
        e.preventDefault();
        setRenderRequest(form);
        navigate("/render");
    }

    return (
        <div className="flex items-center justify-center min-h-screen bg-gray-950">
            <div className="w-full max-w-lg p-8 flex flex-col gap-6 bg-gray-900 rounded-2xl shadow-2xl">

                <h1 className="text-2xl font-bold text-green-400">Distributed Path Tracer</h1>

                <form onSubmit={handleSubmit} className="flex flex-col gap-6">

                    {/* Scene */}
                    <section className="flex flex-col gap-3">
                        <h2 className="text-xs font-semibold uppercase tracking-widest text-green-600">Scene</h2>
                        <Field label="Scene Name" name="sceneName" value={form.sceneName} onChange={handleChange} />
                        <Field label="S3 Bucket" name="sceneBucket" value={form.sceneBucket} onChange={handleChange} />
                        <Field label="S3 Key" name="sceneKey" value={form.sceneKey} onChange={handleChange} />
                    </section>

                    {/* Render Settings */}
                    <section className="flex flex-col gap-3">
                        <h2 className="text-xs font-semibold uppercase tracking-widest text-green-600">Render Settings</h2>
                        <div className="grid grid-cols-3 gap-3">
                            <Field label="Workers" name="numWorkers" type="number" value={form.numWorkers} onChange={handleChange} />
                            <Field label="Samples" name="numSamples" type="number" value={form.numSamples} onChange={handleChange} />
                            <Field label="Bounces" name="numBounces" type="number" value={form.numBounces} onChange={handleChange} />
                        </div>
                    </section>

                    {/* Resolution */}
                    <section className="flex flex-col gap-3">
                        <h2 className="text-xs font-semibold uppercase tracking-widest text-green-600">Resolution</h2>
                        <div className="grid grid-cols-2 gap-3">
                            <Field label="Width (px)" name="X" type="number" value={form.X} onChange={handleChange} />
                            <Field label="Height (px)" name="Y" type="number" value={form.Y} onChange={handleChange} />
                        </div>
                    </section>

                    <button
                        type="submit"
                        className="w-full rounded-md bg-green-600 hover:bg-green-500 text-white font-semibold py-2.5 transition-colors cursor-pointer"
                    >
                        Start Render
                    </button>

                </form>
            </div>
        </div>
    );
}
