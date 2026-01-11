/**
 * Polyfills for Hermes WebWorker environment
 *
 * This file imports and installs polyfills for APIs that are missing in Hermes.
 * It is bundled into a single file and executed before any user code in workers.
 */

// AbortController polyfill
import 'abort-controller/polyfill';
