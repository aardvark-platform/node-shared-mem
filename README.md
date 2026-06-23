# node-shared-mem

[![npm version](https://img.shields.io/npm/v/node-shared-mem)](https://www.npmjs.com/package/node-shared-mem)
[![npm downloads](https://img.shields.io/npm/d18m/node-shared-mem)](https://www.npmjs.com/package/node-shared-mem)
[![License](https://img.shields.io/npm/l/node-shared-mem)](https://www.npmjs.com/package/node-shared-mem)

High-performance Node.js native addon (`napi`) for mapping existing cross-process shared memory files. Handles V8 pointer compression and V8 Virtual Memory Cage compatibility transparently via automatic copy fallbacks.

**Note:** This module does not create shared memory blocks. Target memory regions must be allocated by an external process before initialization.

## Features

* **Dual-Mode Compatibility**: Uses zero-copy memory wrapping when possible, or explicit memory syncing when the V8 cage is active.
* **Cross-Platform**: Supports Windows, Linux, and MacOS.

## Installation & Compilation

Requires `node-gyp`, system build tools (`g++`, `make`, or Visual Studio Build Tools), and `Python`.

```bash
git clone https://github.com/aardvark-platform/node-shared-mem.git
cd node-shared-mem
npm install
npm run test
npm run test:electron
```
## Usage

Because V8 sandbox configurations vary, you must check `mem.requiresCopy` to determine if manual memory synchronization is required.

### Reading Data
```javascript
const { SharedMemory, MemoryAccess } = require('node-shared-mem');

const mem = new SharedMemory("existing_shared_zone_name", 1024, MemoryAccess.READ); // Access permissions are optional, default is read and write
const buffer = mem.buffer; // Access the underlying ArrayBuffer
const view = new Uint8Array(buffer);

// Explicitly pull data from shared memory if required by V8 cage.
// This is a no-op if the V8 memory cage is disabled.
if (mem.requiresCopy) {
  mem.copyFrom();
}

console.log(view[0]);
```

### Writing Data
```javascript
const { SharedMemory } = require('node-shared-mem');

const mem = new SharedMemory("existing_shared_zone_name", 1024);
const buffer = mem.buffer;
const view = new Uint8Array(buffer);

view[0] = 42;

// Explicitly push data back to shared memory if required by V8 cage.
// This is a no-op if the V8 memory cage is disabled.
if (mem.requiresCopy) {
  mem.copyTo();
}
```

### Subregion Copying (Partial Syncing)
Both `copyFrom()` and `copyTo()` accept three optional parameters to synchronize specific slices of memory rather than the entire buffer:

`mem.copyFrom([srcOffset], [dstOffset], [length])`  
`mem.copyTo([srcOffset], [dstOffset], [length])`

```javascript
// Example: Copy a 256-byte segment starting at byte 128
if (mem.requiresCopy) {
  const srcOffset = 128;
  const dstOffset = 128;
  const length = 256;
  
  mem.copyFrom(srcOffset, dstOffset, length);
}
```

## Architecture: V8 Cage Constraints

V8 pointer compression mandates that all JavaScript `ArrayBuffer` backing stores reside within a single pre-allocated virtual memory sandbox (4GB–16GB). Raw memory pointers opened via OS hooks (`shm_open` or `OpenFileMappingW`) live outside this space and trigger fatal exceptions if wrapped directly inside a standard ArrayBuffer.

`node-shared-mem` resolves this internally:
1. **Cage Disabled**: Wraps the shared memory pointer directly into `mem.buffer` (Zero-Copy mode). `mem.requiresCopy` resolves to `false`.
2. **Cage Enabled**: Allocates `mem.buffer` inside the safe V8 cage. `mem.requiresCopy` resolves to `true`. The user must call `mem.copyFrom()` and `mem.copyTo()` to explicitly synchronize data between the V8 internal buffer and the external shared memory segment.

## Publishing New Versions

Releases are automated via GitHub CI whenever a new semantic Git tag is pushed.

1. Bump the version, commit, and tag atomically using `npm version`:
   ```bash
   # Options: patch, minor, or major
   npm version minor -m "chore: bump version to %s"
   ```

2. Push the commit and the new tag to GitHub to trigger the release workflow:
   ```bash
   git push --follow-tags
   ```

## License

[MIT](LICENSE)
