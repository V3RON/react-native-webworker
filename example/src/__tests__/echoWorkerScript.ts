/**
 * Echo Worker Script
 *
 * A simple worker script that echoes back any message it receives.
 * Used for E2E testing of the Structured Clone implementation.
 */
export const echoWorkerScript = `
  self.onmessage = function(e) {
    self.postMessage(e.data);
  };
`;
