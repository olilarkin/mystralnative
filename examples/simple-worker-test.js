/**
 * Simple Worker Test
 */

console.log('=== Simple Worker Test ===');

// Very simple worker code with error handling
const code = `
console.log('Worker: Step 1');
var x = 42;
console.log('Worker: Step 2, x =', x);
console.log('Worker: typeof self =', typeof self);
console.log('Worker: self =', self);
console.log('Worker: self.onmessage =', self.onmessage);
try {
    self.onmessage = function(e) {
        console.log('Worker: Got message');
        postMessage('Hello back');
    };
    console.log('Worker: Step 3 - onmessage set successfully');
} catch (err) {
    console.error('Worker: Error setting onmessage:', err);
}
console.log('Worker: Step 4 - done');
`;

console.log('Creating worker...');
const worker = new Worker(new Blob([code]));

worker.onmessage = (e) => {
    console.log('Main: Received:', e.data);
};

console.log('Posting message...');
worker.postMessage('Hello worker');

console.log('Waiting...');

setTimeout(() => {
    console.log('Terminating worker...');
    worker.terminate();
    console.log('Done');
}, 3000);
