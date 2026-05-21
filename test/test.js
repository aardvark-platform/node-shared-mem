const { spawn } = require('child_process');
const readline = require('readline');
const tape = require('tape');
const SharedMemory = require('../');

function str(buffer, len) {
    const a = new Uint8Array(buffer);
    let i = 0;
    let res = '';
    while (a[i] != 0 && i < len) {
        res += String.fromCharCode(a[i]);
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

const name = 'testfile';
const length = 1024;
const testData = 'Hello my dudes!';
const server = spawn('dotnet', ['run', '--project', 'test/server/Server/Server.fsproj', '--', name, length, testData]);

let serverReadyResolver;
const serverReadyPromise = new Promise((resolve) => { serverReadyResolver = resolve; });

let testsStarted = false;

const bootTimeout = setTimeout(() => {
    console.error('FAILED: .NET process took too long to start.');
    if (server) server.kill();
    process.exit(1);
}, 30000);

server.on('exit', (code) => {
    if (!testsStarted) {
        clearTimeout(bootTimeout);
        console.error(`FAILED: .NET process exited unexpectedly with code ${code}`);
        process.exit(1);
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
        clearTimeout(bootTimeout);
        testsStarted = true;
        serverReadyResolver();
    }
});

const stderr = readline.createInterface({
    input: server.stderr,
    terminal: false
});

stderr.on('data', (data) => {
    console.error(`[.NET Error]: ${data}`);
});

tape('Setup .NET server', async (t) => {
    await serverReadyPromise;
    t.pass('.NET process is online and listening.');
    t.end();
});

tape('Read from external buffer', function (t) {
    const mem = new SharedMemory.SharedMemory(name, length);

    t.strictEqual(mem.name, name, 'name is valid');
    t.strictEqual(mem.length, length, 'length is valid');
    t.strictEqual(mem.requiresCopy, false, 'requiresCopy is false');

    let data = str(mem.buffer, length);
    t.strictEqual(data, testData, 'data is valid before copy()');

    mem.copyFrom(); // NOP

    data = str(mem.buffer, length);
    t.strictEqual(data, testData, 'data is valid after copy() (nop)');

    mem.close();

    t.strictEqual(mem.name, undefined, 'name is undefined after close()');
    t.strictEqual(mem.length, undefined, 'length is undefined after close()');
    t.strictEqual(mem.requiresCopy, undefined, 'requiresCopy is undefined after close()');
    t.strictEqual(mem.buffer, undefined, 'buffer is undefined after close()');

    t.pass('finished');
    t.end();
});

tape('Shutdown server', async (t) => {
    if (server) {
        server.stdin.write('\n');

        try {
            const status = await waitForProcessToExit(server, 3000);
            console.log(`Server exited with code ${status}`);
            delete server;
        } catch (error) {
            console.error(error.message);
            server.kill();
        }
    }
    t.end();
});