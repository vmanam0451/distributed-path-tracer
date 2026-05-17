# distributed-path-tracer

A naive implementation of a distributed path tracer. 

# Demo

[![Sample Render](https://img.youtube.com/vi/A9Vivr1lrtQ/0.jpg)](https://www.youtube.com/watch?v=A9Vivr1lrtQ)

# Code Overview:

**path-tracer-web**: 
 - **path-tracer-backend**: Upon a request from the frontend, downloads a gltf file from a specified location on S3. 
   Splits the scene's primitives among workers and launches them. Receieves rendered pixels from the master worker which it forwards to the frontend for display.
 - **path-tracer-frontend**: Displays the rendering settings form (scene location, how many workers, resolution, # of samples, # of bounces). Shows the rendered pixels receieved from the backend. 

 **path-tracer-core**:
 - **worker**: Downloads the meshes specified by the backend. Generates rays for the portion of the resolution. Communicates with other workers for intersection tests as scene is split globally among all N workers. Forwards finished rays to master for accumulation. 
 - **master**: Accumulates pixels and sends them back to the backend.

 Current implementation is not as efficient mainly due to the fact that the scene is split among all N workers. Each worker has to do a broadcast and gather for each ray it generates i.e to run an intersection test, a worker cannot do it locally as it does not have the complete scene data. 
 It has to forward that ray to all the other workers. All the other workers perform an intersection test and send the results back to the originating worker so that it can collate the results. 

 **Future Work**:
 - **Smarter scene partitioning**: Biggest source of inefficency is the broadcast and gather needed to do intersection tests. Investigate different methods to alleivate this. For ex, maybe split a scene spaitally rather than by primitive. Each worker can have AABB such that it locally run a high-level intersection test that returns a subset of workers. Distributed octree?
 - **Microservice oriented design**: Currently a worker does everything (ray generation, intersection, shading). Might be better to create specialized workers that do a dedicated tasks as each role can independently scale.
 - **GPU intersection**: Investigate using CUDA to perform intersections on a GPU.
 - **Fault tolerance**: Currently, no fault-tolerance exists if a master/worker dies.
 - **Binary format**: Currently, workers communicate with JSON over TCP. Might be better if data was binary encoded.


# Credits
**Ray Ferric** - For providing most of the core path tracing library https://github.com/rayferric/path-tracer/tree/main. The changes I made were: how a scene is loaded and the core path tracing tracing algorithm itself as my implementation is iterative rather than recursive.

**Florian Amsallem** - For writing this cool article on KD Trees. https://flomonster.fr/articles/kdtree.html  
