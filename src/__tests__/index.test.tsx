import { Worker, WorkerPool } from '../index';

// Mock the native module
jest.mock('../NativeWebworker', () => ({
  createWorker: jest.fn().mockResolvedValue('test-worker'),
  createWorkerWithScript: jest.fn().mockResolvedValue('test-worker'),
  terminateWorker: jest.fn().mockResolvedValue(true),
  postMessage: jest.fn().mockResolvedValue(true),
  evalScript: jest.fn().mockResolvedValue('result'),
}));

describe('Worker', () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  it('should create a worker with inline script', async () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
      name: 'test-worker',
    });

    expect(worker).toBeDefined();
    await worker.terminate();
  });

  it('should create a worker with script path', async () => {
    const worker = new Worker({
      scriptPath: '/path/to/script.js',
      name: 'test-worker',
    });

    expect(worker).toBeDefined();
    await worker.terminate();
  });

  it('should throw error if no script provided', () => {
    expect(() => {
      new Worker({} as any);
    }).toThrow('Either script or scriptPath must be provided');
  });

  it('should post messages to worker', async () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
      name: 'test-worker',
    });

    await worker.postMessage({ data: 'test' });
    await worker.terminate();
  });

  it('should set and get onmessage handler', () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    const handler = jest.fn();
    worker.onmessage = handler;

    expect(worker.onmessage).toBe(handler);
  });

  it('should add and remove event listeners', () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    const handler = jest.fn();
    worker.addEventListener('message', handler);
    worker.removeEventListener('message', handler);
  });

  it('should evaluate scripts in worker', async () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    const result = await worker.eval('1 + 1');
    expect(result).toBe('result');
    await worker.terminate();
  });

  it('should throw error when posting to terminated worker', async () => {
    const worker = new Worker({
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    await worker.terminate();

    await expect(worker.postMessage('test')).rejects.toThrow(
      'Worker has been terminated'
    );
  });
});

describe('WorkerPool', () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  it('should create a pool with specified size', () => {
    const pool = new WorkerPool({
      size: 4,
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
      name: 'test-pool',
    });

    expect(pool.getSize()).toBe(4);
  });

  it('should broadcast messages to all workers', async () => {
    const pool = new WorkerPool({
      size: 2,
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    await pool.broadcast({ action: 'init' });
    await pool.terminate();
  });

  it('should throw error when running on terminated pool', async () => {
    const pool = new WorkerPool({
      size: 2,
      script: 'self.onmessage = (e) => self.postMessage(e.data);',
    });

    await pool.terminate();

    await expect(pool.run([1, 2])).rejects.toThrow(
      'WorkerPool has been terminated'
    );
  });
});
