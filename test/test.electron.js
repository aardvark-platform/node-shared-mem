const { spawn } = require('child_process');
const readline = require('readline');
const assert = require('assert');
const { SharedMemory, MemoryAccess } = require('../');

function str(buffer, offset, len) {
    const a = new Uint8Array(buffer);
    let i = 0;
    let res = '';
    while (a[i + offset] != 0 && i < len) {
        res += String.fromCharCode(a[i + offset]);
        i++;
    }
    return res;
}

function waitForProcessToExit(processObj, timeoutMs) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            reject(new Error('Timed out when waiting for process to exit.'));
        }, timeoutMs);

        processObj.on('exit', (code) => {
            clearTimeout(timer);
            resolve(code);
        });
    });
}

describe('Electron shared memory test', () => {
    let server;
    const name = 'testfile';
    const length = 1024;
    const testData = 'Hello my dudes!';

    beforeEach(function (done) {
        this.timeout(180000);
        let testsStarted = false;

        console.log('Building and spawning server...');
        server = spawn('dotnet', ['run', '--project', 'test/server/Server/Server.fsproj', '--', name, length, testData]);

        server.on('exit', (code) => {
            if (!testsStarted) {
                const errorMsg = `[.NET Error]: Process exited unexpectedly with code ${code}`;
                done(new Error(errorMsg));
            }
        });

        const stdout = readline.createInterface({
            input: server.stdout,
            terminal: false
        });

        stdout.on('line', (data) => {
            const output = data.toString();
            console.log(`[.NET]: ${output.trim()}`);

            if (output.includes('Ready')) {
                testsStarted = true;
                done();
            }
        });

        const stderr = readline.createInterface({
            input: server.stderr,
            terminal: false
        });

        stderr.on('data', (data) => {
            console.error(`[.NET Error]: ${data}`);
        });
    });

    afterEach(async () => {
        if (server) {
            try {
                server.stdin.write('\n');
                const status = await waitForProcessToExit(server, 3000);
                console.log(`Server exited with code ${status}`);
                delete server;
            } catch (error) {
                console.error(error.message);
                server.kill();
            }
        }
    });

    const testCases = [
        { slice: true },
        { slice: false}
    ];

    testCases.forEach(({slice}) => {
        const desc = slice ? ' (with offset and length)' : '';

        it(`Copy from shared memory${desc}`, () => {
            const mem = new SharedMemory(name, length, MemoryAccess.READ);
            assert.strictEqual(mem.name, name);
            assert.strictEqual(mem.length, length);
            assert.strictEqual(mem.requiresCopy, true);

            const [srcOffset, dstOffset, copyLength] = slice ? [3, 1, 5] : [0, 0, testData.length];

            mem.copyFrom(srcOffset, dstOffset, copyLength);

            data = str(mem.buffer, dstOffset, copyLength);
            assert.strictEqual(data, testData.substring(srcOffset, srcOffset + copyLength));

            mem.close();

            assert.strictEqual(mem.name, undefined);
            assert.strictEqual(mem.length, undefined);
            assert.strictEqual(mem.requiresCopy, undefined);
            assert.strictEqual(mem.buffer, undefined);
        });

        it(`Copy to shared memory${desc}`, () => {
            const memWrite = new SharedMemory(name, length, MemoryAccess.WRITE);
            assert.strictEqual(memWrite.name, name);
            assert.strictEqual(memWrite.length, length);
            assert.strictEqual(memWrite.access, MemoryAccess.WRITE);
            assert.strictEqual(memWrite.requiresCopy, true);

            const memRead = new SharedMemory(name, length, MemoryAccess.READ);
            assert.strictEqual(memRead.name, name);
            assert.strictEqual(memRead.length, length);
            assert.strictEqual(memRead.access, MemoryAccess.READ);
            assert.strictEqual(memRead.requiresCopy, true);

            memRead.copyFrom();
            data = str(memRead.buffer, 0, testData.length);
            assert.strictEqual(data, testData);

            const newTestData = 'Here is some new data';
            const encoder = new TextEncoder()
            const count = encoder.encodeInto(newTestData, new Uint8Array(memWrite.buffer));
            assert(count.written > 0, 'Nothing written');

            const [srcOffset, dstOffset, copyLength] = slice ? [3, 1, 5] : [0, 0, newTestData.length];
            memWrite.copyTo(srcOffset, dstOffset, copyLength);

            memRead.copyFrom(dstOffset, 0, copyLength);
            data = str(memRead.buffer, 0, copyLength);
            assert.strictEqual(data, newTestData.substring(srcOffset, srcOffset + copyLength));

            memWrite.close();
            assert.strictEqual(memWrite.name, undefined);
            assert.strictEqual(memWrite.length, undefined);
            assert.strictEqual(memWrite.requiresCopy, undefined);
            assert.strictEqual(memWrite.buffer, undefined);

            memRead.close();
            assert.strictEqual(memRead.name, undefined);
            assert.strictEqual(memRead.length, undefined);
            assert.strictEqual(memRead.requiresCopy, undefined);
            assert.strictEqual(memRead.buffer, undefined);
        });
    });
});