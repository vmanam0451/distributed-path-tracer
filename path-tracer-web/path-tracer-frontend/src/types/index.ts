export default interface RenderRequest {
    sceneBucket: string,
    sceneKey: string,
    sceneName: string,
    numWorkers: number,
    numSamples: number,
    numBounces: number,
    X: number,
    Y: number,
}