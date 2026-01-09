<div align="center">

<h1>react-native-webworker</h1>

<p><em>True multithreading for React Native, powered by JSI</em></p>

![TypeScript](https://img.shields.io/badge/TypeScript-5.0+-3178C6?style=for-the-badge&logo=typescript&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-iOS%20%7C%20Android%20%7C%20Web-000000?style=for-the-badge&logo=react&logoColor=61DAFB)

<br>

🚀 **Execute heavy JavaScript logic on background threads with zero UI blocking**

---

</div>

> ⚠️ **EXPERIMENTAL: EARLY PREVIEW**
>
> This project is currently in a very early stage of development. APIs are unstable, features are missing, and dragons are roaming freely.
> **Expect breaking changes in every version.** Use at your own risk.

---

## Why this library exists?

React Native's single-threaded nature means heavy JavaScript computations (data processing, encryption, complex logic) can freeze the UI, leading to dropped frames and a poor user experience.

While solutions like **Reanimated Worklets** exist and even allow creating independent runtimes, they are primarily designed for specific callbacks (animations, gestures, frame processing). `react-native-webworker` fills the gap for **persistent, heavy background processing** using the standard Web Worker API.

### Workers vs. Worklets

It is important to distinguish between **Worklets** (used by Reanimated, VisionCamera, etc.) and **Web Workers** (this library).

| Feature | Worklets | Web Workers (This Library) |
| :--- | :--- | :--- |
| **Primary Goal** | **Callbacks**: High-frequency, specialized logic (Animations, Sensors, Video). | **Computation**: Persistent offloading of CPU-intensive business logic. |
| **Lifecycle** | **Short-lived**: Usually invoked for specific events or frames. | **Persistent**: Long-running threads that maintain internal state. |
| **Execution** | Often tied to the UI thread or specific specialized threads. | True Parallelism on dedicated background threads. |
| **Standard** | Custom React Native concepts (Runtime/Worklet). | **W3C Web Standard** (`postMessage`, `onmessage`). |
| **Compatibility** | Native-only. | **Isomorphic** (runs on Web and Native). |

**Use `react-native-webworker` when:**
- You need to process large datasets (filtering, sorting, mapping).
- You are performing complex mathematical calculations or encryption.
- You need code that runs exactly the same on React Native and the Web.
- You need a persistent background task that manages its own state over time.

## Web Compatibility

One of the core design goals of `react-native-webworker` is **isomorphic code**. The API is designed to strictly follow the [MDN Web Worker API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API).

This means you can write your worker logic once and use it in:
1.  **React Native** (Android & iOS via JSI)
2.  **React Web** (Standard Browser Web Workers)

```typescript
// worker.js
// This code works in the Browser AND React Native
self.onmessage = (event) => {
  const result = heavyCalculation(event.data);
  self.postMessage(result);
};
```

## Installation

```bash
npm install react-native-web-worker
# or
yarn add react-native-web-worker
# or
pnpm add react-native-web-worker
# or
bun add react-native-web-worker
```

## Usage

### Basic Worker

Create a dedicated worker for a specific task.

```typescript
import { Worker } from 'react-native-webworker';

const worker = new Worker({
  script: `
    onmessage = (e) => {
      // Heavy computation here
      const result = e.data * 2;
      postMessage(result);
    };
  `
});

worker.onmessage = (e) => {
  console.log('Result:', e.data);
};

worker.postMessage(42);
```

### Worker Pool

For parallelizing large datasets or multiple independent tasks.

```typescript
import { WorkerPool } from 'react-native-webworker';

const pool = new WorkerPool({
  size: 4, // Number of threads
  script: `
    onmessage = (e) => {
      postMessage(e.data * e.data);
    };
  `
});

// Automatically distributes tasks across the pool
const results = await pool.run([1, 2, 3, 4, 5]);
console.log(results); // [1, 4, 9, 16, 25]

await pool.terminate();
```

## API Reference

### `Worker`

The primary class for creating a background thread.

**Options:**
- `script`: Inline JavaScript string to execute in the worker.
- `scriptPath`: Path to a JavaScript file (Asset/Bundle).
- `name`: Optional identifier for debugging.

**Methods:**
- `postMessage(data)`: Send data to the worker.
- `terminate()`: Kill the worker thread immediately.
- `addEventListener(type, handler)`: Listen for `message` events.

### `WorkerPool`

Manages a collection of workers to handle tasks in parallel.

**Options:**
- `size`: Number of workers to spawn.
- `script` / `scriptPath`: The script each worker will run.

**Methods:**
- `run(tasks)`: Distributes an array of tasks and returns a promise that resolves with all results.
- `broadcast(data)`: Sends the same message to all workers in the pool.
- `terminate()`: Shuts down all workers in the pool.

## Contributing

Contributions are welcome! Please see the [Contributing Guide](CONTRIBUTING.md) for setup instructions.

## License

MIT © [Szymon Chmal](https://github.com/V3RON)
