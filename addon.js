var addon = require('bindings')('node_shared_mem');

/**
 * Access permissions for shared memory mappings.
 * @enum {number}
 */
const MemoryAccess = Object.freeze({
    READ:    addon.MemoryAccess.READ,
    WRITE:   addon.MemoryAccess.WRITE,
    EXECUTE: addon.MemoryAccess.EXECUTE
});

/**
 * Represents a shared memory mapping.
 */
class SharedMemory {

    /**
     * Opens the shared memory mapping with the given name.
     * @param {string} name Name of the mapping.
     * @param {number} length Length of the mapping in bytes.
     * @param {MemoryAccess} [access] Access permissions of the mapping. Default is {@link MemoryAccess.READ} and {@link MemoryAccess.WRITE}. 
     */
    constructor (name, length, access) {
        const native = new addon.SharedMemory(name, length, access);
        this._native = native;

        /**
         * Name of the mapping.
         * @type {string}
         */
        this.name = native.name;

        /**
         * Length of the mapping in bytes.
         * @type {string}
         */
        this.length = native.length;

        /**
         * Access permissions of the mapping.
         * @type {MemoryAccess}
         */
        this.access = native.access;

        /**
         * Underlying array buffer of the mapping.
         * @type {ArrayBuffer}
         */
        this.buffer = native.buffer;

        /**
         * Indicates whether data must be explicitly synchronized between shared memory and the array buffer.
         * If `false`, the array buffer is backed by the shared memory mapping.
         * @type {boolean}
         */
        this.requiresCopy = native.requiresCopy;
    }

    /**
     * Copy data from shared memory to the underlying array buffer.
     * @remarks Only required if the buffer is not backed by shared memory as indicated by `requiresCopy`.
     * @param {*} [srcOffset] Byte offset into the shared memory mapping. Default is 0.
     * @param {*} [dstOffset] Byte offset into the array buffer. Default is 0.
     * @param {*} [length] Number of bytes to copy. Defaults to the number of available bytes.
     */
    copyFrom(srcOffset, dstOffset, length) {
        this._native.copyFrom(srcOffset, dstOffset, length);
    }

    /**
     * Copy data the underlying array buffer to shared memory.
     * @remarks Only required if the buffer is not backed by shared memory as indicated by `requiresCopy`.
     * @param {*} [srcOffset] Byte offset into the array buffer. Default is 0.
     * @param {*} [dstOffset] Byte offset into the shared memory mapping. Default is 0.
     * @param {*} [length] Number of bytes to copy. Defaults to the number of available bytes.
     */
    copyTo(srcOffset, dstOffset, length) {
        this._native.copyTo(srcOffset, dstOffset, length);
    }

    /**
     * Closes the mapping.
     */
    close() {
        this._native.close();
        delete this.name;
        delete this.length;
        delete this.access;
        delete this.buffer;
        delete this.requiresCopy;
        delete this._native;
    }
}

module.exports = { SharedMemory, MemoryAccess };