/**
 * MystralNative WebWorker and Image API Test
 *
 * Tests:
 * 1. Image() class - DOM-compatible image loading
 * 2. Worker - Web Worker support for background computation
 */

console.log('=== MystralNative Workers Test ===');

// Test 1: Image() class
console.log('\n--- Test 1: Image() class ---');

const img = new Image();
img.onload = () => {
    console.log(`Image loaded: ${img.width}x${img.height}`);
    console.log(`  naturalWidth: ${img.naturalWidth}`);
    console.log(`  naturalHeight: ${img.naturalHeight}`);
    console.log(`  complete: ${img.complete}`);
};
img.onerror = (e) => {
    console.error('Image load error:', e.error);
};

// Load a test image (use a local asset - path relative to where mystral is run from)
img.src = './examples/assets/DamagedHelmet.glb';  // Use an existing asset for now

// Test 2: WebWorkers
console.log('\n--- Test 2: WebWorkers ---');

const workerCode = `
    console.log('Worker: Starting...');

    self.onmessage = (e) => {
        console.log('Worker: Received message:', JSON.stringify(e.data));

        // Simulate some computation
        let result = 0;
        for (let i = 0; i < e.data.iterations; i++) {
            result += Math.sqrt(i);
        }

        console.log('Worker: Computation complete, result =', result);
        postMessage({ result: result, input: e.data });
    };

    console.log('Worker: Ready');
`;

// Create worker from Blob
const blob = new Blob([workerCode], { type: 'application/javascript' });
const worker = new Worker(blob);

worker.onmessage = (e) => {
    console.log('Main: Received from worker:', JSON.stringify(e.data));
    console.log('Main: Result =', e.data.result);

    // Terminate worker after receiving result
    worker.terminate();
    console.log('Main: Worker terminated');
};

worker.onerror = (e) => {
    console.error('Main: Worker error:', e);
};

// Send work to the worker
console.log('Main: Sending message to worker...');
worker.postMessage({ iterations: 1000, message: 'Hello from main!' });

// Test 3: Multiple workers
console.log('\n--- Test 3: Multiple Workers ---');

const workers = [];
const workerResults = [];

for (let i = 0; i < 3; i++) {
    const code = `
        self.onmessage = (e) => {
            const workerId = ${i};
            console.log('Worker ' + workerId + ': Processing...');
            // Simulate varying workload
            let sum = 0;
            for (let j = 0; j < e.data.count; j++) {
                sum += j * ${i + 1};
            }
            console.log('Worker ' + workerId + ': Done, sum =', sum);
            postMessage({ workerId: workerId, sum: sum });
        };
    `;

    const w = new Worker(new Blob([code]));
    w.onmessage = (e) => {
        workerResults.push(e.data);
        console.log(`Main: Worker ${e.data.workerId} completed with sum = ${e.data.sum}`);

        if (workerResults.length === 3) {
            console.log('Main: All workers completed!');
            const totalSum = workerResults.reduce((a, b) => a + b.sum, 0);
            console.log('Main: Total sum =', totalSum);
        }
    };
    workers.push(w);
}

// Start all workers
workers.forEach((w, i) => {
    w.postMessage({ count: 100 });
});

// Clean up workers after a delay
setTimeout(() => {
    workers.forEach(w => w.terminate());
    console.log('Main: All workers terminated');
    console.log('\n=== All tests complete ===');
}, 2000);
