/**
 * Parallel Computing Demo
 *
 * Demonstrates the power of WebWorkers by comparing:
 * - Sequential Mandelbrot calculation on JS thread
 * - Parallel Mandelbrot calculation using WorkerPool
 */

import React, { useState, useCallback } from 'react';
import {
  View,
  Text,
  TouchableOpacity,
  StyleSheet,
  ScrollView,
  ActivityIndicator,
} from 'react-native';
import { WorkerPool } from 'react-native-webworker';

// Configuration - increased for meaningful computation time
const WIDTH = 200;
const HEIGHT = 150;
const MAX_ITERATIONS = 10000;
const X_MIN = -2.5;
const X_MAX = 1.0;
const Y_MIN = -1.25;
const Y_MAX = 1.25;
const POOL_SIZE = 4;

interface MandelbrotResult {
  data: number[][];
  duration: number;
}

// Sequential Mandelbrot calculation (runs on JS thread)
const calculateMandelbrotSequential = (): MandelbrotResult => {
  const startTime = Date.now();
  const data: number[][] = [];

  for (let py = 0; py < HEIGHT; py++) {
    const row: number[] = [];
    for (let px = 0; px < WIDTH; px++) {
      const x0 = X_MIN + (px / WIDTH) * (X_MAX - X_MIN);
      const y0 = Y_MIN + (py / HEIGHT) * (Y_MAX - Y_MIN);

      let x = 0;
      let y = 0;
      let iteration = 0;

      while (x * x + y * y <= 4 && iteration < MAX_ITERATIONS) {
        const xTemp = x * x - y * y + x0;
        y = 2 * x * y + y0;
        x = xTemp;
        iteration++;
      }

      row.push(iteration);
    }
    data.push(row);
  }

  return {
    data,
    duration: Date.now() - startTime,
  };
};

// Worker script for parallel Mandelbrot calculation
const workerScript = `
  const MAX_ITERATIONS = ${MAX_ITERATIONS};
  const X_MIN = ${X_MIN};
  const X_MAX = ${X_MAX};
  const Y_MIN = ${Y_MIN};
  const Y_MAX = ${Y_MAX};

  self.onmessage = function(event) {
    const { startRow, endRow, width, height } = event.data;
    const rows = [];

    for (let py = startRow; py < endRow; py++) {
      const row = [];
      for (let px = 0; px < width; px++) {
        const x0 = X_MIN + (px / width) * (X_MAX - X_MIN);
        const y0 = Y_MIN + (py / height) * (Y_MAX - Y_MIN);

        let x = 0;
        let y = 0;
        let iteration = 0;

        while (x * x + y * y <= 4 && iteration < MAX_ITERATIONS) {
          const xTemp = x * x - y * y + x0;
          y = 2 * x * y + y0;
          x = xTemp;
          iteration++;
        }

        row.push(iteration);
      }
      rows.push(row);
    }

    self.postMessage({
      startRow: startRow,
      rows: rows
    });
  };
`;

interface WorkerTask {
  startRow: number;
  endRow: number;
  width: number;
  height: number;
}

interface WorkerResult {
  startRow: number;
  rows: number[][];
}

export const ParallelDemo: React.FC = () => {
  const [sequentialResult, setSequentialResult] =
    useState<MandelbrotResult | null>(null);
  const [parallelResult, setParallelResult] = useState<MandelbrotResult | null>(
    null
  );
  const [isRunning, setIsRunning] = useState(false);
  const [currentTask, setCurrentTask] = useState<string | null>(null);

  const runSequential = useCallback(() => {
    setIsRunning(true);
    setCurrentTask('Running on JS Thread...');
    setParallelResult(null);

    // Use setTimeout to allow UI to update before blocking
    setTimeout(() => {
      const result = calculateMandelbrotSequential();
      setSequentialResult(result);
      setIsRunning(false);
      setCurrentTask(null);
    }, 100);
  }, []);

  const runParallel = useCallback(async () => {
    setIsRunning(true);
    setCurrentTask('Creating Worker Pool...');
    setSequentialResult(null);

    try {
      // Create pool (this has setup overhead)
      const pool = new WorkerPool<WorkerTask, WorkerResult>({
        size: POOL_SIZE,
        script: workerScript,
        name: 'mandelbrot-pool',
      });

      // Split work into chunks for each worker
      const rowsPerWorker = Math.ceil(HEIGHT / POOL_SIZE);
      const tasks: WorkerTask[] = [];

      for (let i = 0; i < POOL_SIZE; i++) {
        const startRow = i * rowsPerWorker;
        const endRow = Math.min(startRow + rowsPerWorker, HEIGHT);
        if (startRow < HEIGHT) {
          tasks.push({
            startRow,
            endRow,
            width: WIDTH,
            height: HEIGHT,
          });
        }
      }

      setCurrentTask('Computing in parallel...');

      // Start timing AFTER pool creation - measure only computation
      const startTime = Date.now();

      // Run all tasks in parallel
      const results = await pool.run(tasks);

      const duration = Date.now() - startTime;

      // Combine results
      const data: number[][] = [];
      for (let i = 0; i < HEIGHT; i++) {
        data[i] = [];
      }
      for (const result of results) {
        for (let i = 0; i < result.rows.length; i++) {
          data[result.startRow + i] = result.rows[i]!;
        }
      }

      await pool.terminate();

      setParallelResult({ data, duration });
      setIsRunning(false);
      setCurrentTask(null);
    } catch (error) {
      console.error('Parallel computation error:', error);
      setIsRunning(false);
      setCurrentTask(null);
    }
  }, []);

  const clearResults = useCallback(() => {
    setSequentialResult(null);
    setParallelResult(null);
  }, []);

  const speedup =
    sequentialResult && parallelResult
      ? (sequentialResult.duration / parallelResult.duration).toFixed(2)
      : null;

  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Parallel Computing Demo</Text>
        <Text style={styles.subtitle}>Mandelbrot Set Calculation</Text>
        <Text style={styles.description}>
          Compare sequential (JS thread) vs parallel (Worker Pool) computation.
          The Mandelbrot set calculation is CPU-intensive and easily
          parallelizable.
        </Text>
      </View>

      <View style={styles.configSection}>
        <Text style={styles.configTitle}>Configuration</Text>
        <Text style={styles.configText}>
          Grid: {WIDTH} x {HEIGHT} pixels
        </Text>
        <Text style={styles.configText}>Max iterations: {MAX_ITERATIONS}</Text>
        <Text style={styles.configText}>Worker pool size: {POOL_SIZE}</Text>
      </View>

      <View style={styles.buttonSection}>
        <TouchableOpacity
          style={[styles.button, styles.sequentialButton]}
          onPress={runSequential}
          disabled={isRunning}
        >
          <Text style={styles.buttonText}>Run Sequential (JS Thread)</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={[styles.button, styles.parallelButton]}
          onPress={runParallel}
          disabled={isRunning}
        >
          <Text style={styles.buttonText}>Run Parallel (Worker Pool)</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={[styles.button, styles.clearButton]}
          onPress={clearResults}
          disabled={isRunning}
        >
          <Text style={styles.buttonText}>Clear Results</Text>
        </TouchableOpacity>
      </View>

      {isRunning && (
        <View style={styles.loadingSection}>
          <ActivityIndicator size="large" color="#007AFF" />
          <Text style={styles.loadingText}>{currentTask}</Text>
        </View>
      )}

      <View style={styles.resultsSection}>
        <Text style={styles.resultsTitle}>Results</Text>

        {sequentialResult && (
          <View style={styles.resultCard}>
            <Text style={styles.resultLabel}>Sequential (JS Thread)</Text>
            <Text style={styles.resultValue}>
              {sequentialResult.duration} ms
            </Text>
          </View>
        )}

        {parallelResult && (
          <View style={styles.resultCard}>
            <Text style={styles.resultLabel}>
              Parallel ({POOL_SIZE} Workers)
            </Text>
            <Text style={styles.resultValue}>{parallelResult.duration} ms</Text>
          </View>
        )}

        {speedup && (
          <View style={[styles.resultCard, styles.speedupCard]}>
            <Text style={styles.resultLabel}>Speedup</Text>
            <Text style={styles.speedupValue}>{speedup}x faster</Text>
          </View>
        )}

        {!sequentialResult && !parallelResult && !isRunning && (
          <Text style={styles.noResultsText}>
            Run a computation to see results
          </Text>
        )}
      </View>

      <View style={styles.infoSection}>
        <Text style={styles.infoTitle}>How It Works</Text>
        <Text style={styles.infoText}>
          <Text style={styles.bold}>Sequential:</Text> Calculates all{' '}
          {WIDTH * HEIGHT} pixels one by one on the main JavaScript thread. This
          blocks UI updates during computation.
        </Text>
        <Text style={styles.infoText}>
          <Text style={styles.bold}>Parallel:</Text> Divides the work into{' '}
          {POOL_SIZE} chunks, with each worker calculating a portion of the
          image simultaneously. The main thread remains responsive.
        </Text>
        <Text style={styles.infoText}>
          The speedup depends on the number of CPU cores and workload size.
          Parallel timing measures only computation (excludes worker setup).
        </Text>
      </View>
    </ScrollView>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  header: {
    padding: 20,
    backgroundColor: '#fff',
    borderBottomWidth: 1,
    borderBottomColor: '#e0e0e0',
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    color: '#333',
  },
  subtitle: {
    fontSize: 18,
    color: '#666',
    marginTop: 4,
  },
  description: {
    fontSize: 14,
    color: '#888',
    marginTop: 8,
    lineHeight: 20,
  },
  configSection: {
    padding: 16,
    backgroundColor: '#fff',
    marginTop: 10,
    marginHorizontal: 10,
    borderRadius: 8,
  },
  configTitle: {
    fontSize: 16,
    fontWeight: '600',
    marginBottom: 8,
  },
  configText: {
    fontSize: 14,
    color: '#666',
    marginBottom: 4,
    fontFamily: 'monospace',
  },
  buttonSection: {
    padding: 10,
  },
  button: {
    padding: 16,
    borderRadius: 8,
    marginBottom: 10,
  },
  sequentialButton: {
    backgroundColor: '#FF9500',
  },
  parallelButton: {
    backgroundColor: '#34C759',
  },
  clearButton: {
    backgroundColor: '#8E8E93',
  },
  buttonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
    textAlign: 'center',
  },
  loadingSection: {
    padding: 20,
    alignItems: 'center',
  },
  loadingText: {
    marginTop: 10,
    fontSize: 16,
    color: '#666',
  },
  resultsSection: {
    padding: 16,
    backgroundColor: '#fff',
    marginHorizontal: 10,
    borderRadius: 8,
  },
  resultsTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    marginBottom: 12,
  },
  resultCard: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: 12,
    backgroundColor: '#f8f8f8',
    borderRadius: 6,
    marginBottom: 8,
  },
  speedupCard: {
    backgroundColor: '#e8f5e9',
  },
  resultLabel: {
    fontSize: 14,
    color: '#333',
  },
  resultValue: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#007AFF',
  },
  speedupValue: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#34C759',
  },
  noResultsText: {
    textAlign: 'center',
    color: '#888',
    fontSize: 14,
    padding: 20,
  },
  visualSection: {
    marginTop: 10,
    padding: 16,
    backgroundColor: '#fff',
    marginHorizontal: 10,
    borderRadius: 8,
  },
  visualTitle: {
    fontSize: 16,
    fontWeight: '600',
    marginBottom: 12,
    textAlign: 'center',
  },
  mandelbrotContainer: {
    alignItems: 'center',
    backgroundColor: '#000',
    padding: 2,
    borderRadius: 4,
  },
  mandelbrotRow: {
    flexDirection: 'row',
  },
  mandelbrotPixel: {
    width: 2,
    height: 2,
  },
  infoSection: {
    padding: 16,
    backgroundColor: '#fff',
    margin: 10,
    borderRadius: 8,
    marginBottom: 30,
  },
  infoTitle: {
    fontSize: 16,
    fontWeight: '600',
    marginBottom: 12,
  },
  infoText: {
    fontSize: 14,
    color: '#666',
    lineHeight: 20,
    marginBottom: 8,
  },
  bold: {
    fontWeight: '600',
    color: '#333',
  },
});

export default ParallelDemo;
