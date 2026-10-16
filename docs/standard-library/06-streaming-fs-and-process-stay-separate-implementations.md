# Streaming: fs and process stay separate implementations

`fs.readFileStream` and `process.spawn`'s event-based output are backed by genuinely different OS mechanisms underneath (file I/O vs. process pipes) and belong to different std modules, so they are implemented independently, not sharing one literal underlying stream type. The two do follow the same API convention by design (`.on("data", ...)`, then a final `.on("end")`/`.on("exit")`), so usage looks near-identical even though the internals differ.

