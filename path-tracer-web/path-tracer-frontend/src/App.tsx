import { useState } from "react";

import {
    BrowserRouter,
    Routes,
    Route,
} from "react-router";

import type RenderRequest from "./types";
import HomePage from "./components/home";
import RenderPage from "./components/render";


function App() {
    const [renderRequest, setRenderRequest] = useState<RenderRequest | null>(null)

    return (
        <BrowserRouter>
            <Routes>
                <Route path="/" element={<HomePage setRenderRequest={setRenderRequest}/>} />
                <Route path="/render" element={<RenderPage renderRequest={renderRequest!} />} />
            </Routes>
        </BrowserRouter>
    )
}

export default App
