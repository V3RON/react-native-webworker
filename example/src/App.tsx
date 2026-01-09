/**
 * Example React Native App using WebWorker Module
 *
 * Demonstrates various worker use cases
 */

import { useState } from 'react';
import {
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
  ActivityIndicator,
} from 'react-native';
import { Worker, WorkerPool } from 'react-native-webworker';
import { ParallelDemo } from './screens/ParallelDemo';

type TabType = 'examples' | 'parallel';

interface LogEntry {
  message: string;
  type: 'info' | 'success' | 'error' | 'warning';
  time: string;
}

const App = () => {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [activeTab, setActiveTab] = useState<TabType>('examples');

  const addLog = (message: string, type: LogEntry['type'] = 'info') => {
    setLogs((prev) => [
      ...prev,
      { message, type, time: new Date().toISOString() },
    ]);
  };

  // Example 1: Simple computation worker
  const runSimpleWorker = async () => {
    addLog('Creating simple worker...');
    setLoading(true);

    try {
      const worker = new Worker({
        script: `
          self.onmessage = function(event) {
            const { numbers } = event.data;
            const sum = numbers.reduce((a, b) => a + b, 0);
            const avg = sum / numbers.length;

            self.postMessage({
              sum: sum,
              average: avg,
              count: numbers.length
            });
          };
        `,
        name: 'simple-compute',
      });

      worker.onmessage = (event) => {
        addLog(`Worker result: ${JSON.stringify(event.data)}`, 'success');
      };

      // Send data to worker
      await worker.postMessage({
        numbers: [1, 2, 3, 4, 5, 10, 20, 30, 40, 50],
      });

      // Clean up after 2 seconds
      setTimeout(async () => {
        await worker.terminate();
        addLog('Worker terminated');
        setLoading(false);
      }, 2000);
    } catch (error) {
      addLog(`Error: ${(error as Error).message}`, 'error');
      setLoading(false);
    }
  };

  // Example 2: Heavy computation (Fibonacci)
  const runHeavyComputation = async () => {
    addLog('Creating computation worker...');
    setLoading(true);

    try {
      const worker = new Worker({
        script: `
          function fibonacci(n) {
            if (n <= 1) return n;
            return fibonacci(n - 1) + fibonacci(n - 2);
          }

          self.onmessage = function(event) {
            const { n } = event.data;
            const startTime = Date.now();
            const result = fibonacci(n);
            const duration = Date.now() - startTime;

            self.postMessage({
              result: result,
              duration: duration,
              n: n
            });
          };
        `,
        name: 'fibonacci',
      });

      worker.onmessage = (event) => {
        const data = event.data as {
          n: number;
          result: number;
          duration: number;
        };
        addLog(
          `Fibonacci(${data.n}) = ${data.result} (${data.duration}ms)`,
          'success'
        );
        setLoading(false);
      };

      // Calculate fibonacci(35) - takes a few seconds
      await worker.postMessage({ n: 35 });

      // Clean up after completion
      setTimeout(async () => {
        await worker.terminate();
        addLog('Worker terminated');
      }, 5000);
    } catch (error) {
      addLog(`Error: ${(error as Error).message}`, 'error');
      setLoading(false);
    }
  };

  // Example 3: Worker Pool (Parallel Processing)
  const runWorkerPool = async () => {
    addLog('Creating worker pool (4 workers)...');
    setLoading(true);

    try {
      const pool = new WorkerPool<
        { chunk: number[]; index: number },
        { index: number; sum: number; count: number }
      >({
        size: 4,
        script: `
          self.onmessage = function(event) {
            const { chunk, index } = event.data;
            const sum = chunk.reduce((a, b) => a + b, 0);

            self.postMessage({
              index: index,
              sum: sum,
              count: chunk.length
            });
          };
        `,
        name: 'pool',
      });

      // Create large dataset
      const data = Array.from({ length: 10000 }, (_, i) => i + 1);
      const chunkSize = Math.ceil(data.length / 4);

      // Split into chunks
      const tasks: { chunk: number[]; index: number }[] = [];
      for (let i = 0; i < 4; i++) {
        const start = i * chunkSize;
        const end = Math.min(start + chunkSize, data.length);
        tasks.push({
          chunk: data.slice(start, end),
          index: i,
        });
      }

      addLog(`Processing ${data.length} items in 4 parallel workers...`);
      const startTime = Date.now();

      // Process in parallel
      const results = await pool.run(tasks);

      const duration = Date.now() - startTime;
      const totalSum = results.reduce((acc, r) => acc + r.sum, 0);

      addLog(
        `Pool completed: sum=${totalSum}, duration=${duration}ms`,
        'success'
      );

      // Terminate pool
      await pool.terminate();
      addLog('Worker pool terminated');
      setLoading(false);
    } catch (error) {
      addLog(`Error: ${(error as Error).message}`, 'error');
      setLoading(false);
    }
  };

  // Example 4: Event-driven worker
  const runEventWorker = async () => {
    addLog('Creating event-driven worker...');
    setLoading(true);

    try {
      const worker = new Worker({
        script: `
          let counter = 0;
          let intervalId = null;

          self.addEventListener('message', function(event) {
            const { action } = event.data;

            if (action === 'start') {
              counter = 0;
              intervalId = setInterval(() => {
                counter++;
                self.postMessage({ type: 'tick', counter: counter });

                if (counter >= 5) {
                  clearInterval(intervalId);
                  self.postMessage({ type: 'done', counter: counter });
                }
              }, 500);
            } else if (action === 'stop') {
              if (intervalId) {
                clearInterval(intervalId);
              }
              self.postMessage({ type: 'stopped', counter: counter });
            }
          });
        `,
        name: 'event-worker',
      });

      worker.addEventListener('message', (event) => {
        const data = event.data as { type: string; counter: number };
        const { type, counter } = data;

        if (type === 'tick') {
          addLog(`Tick ${counter}`, 'info');
        } else if (type === 'done') {
          addLog(`Event worker completed (${counter} ticks)`, 'success');
          setLoading(false);
        } else if (type === 'stopped') {
          addLog(`Event worker stopped at ${counter}`, 'warning');
          setLoading(false);
        }
      });

      // Start the worker
      await worker.postMessage({ action: 'start' });

      // Auto cleanup
      setTimeout(async () => {
        await worker.terminate();
        addLog('Event worker terminated');
      }, 5000);
    } catch (error) {
      addLog(`Error: ${(error as Error).message}`, 'error');
      setLoading(false);
    }
  };

  // Example 5: String processing
  const runStringProcessing = async () => {
    addLog('Creating string processing worker...');
    setLoading(true);

    try {
      const worker = new Worker<
        { text: string; operation: string },
        { operation: string; result: unknown }
      >({
        script: `
          self.onmessage = function(event) {
            const { text, operation } = event.data;
            let result;

            switch(operation) {
              case 'uppercase':
                result = text.toUpperCase();
                break;
              case 'reverse':
                result = text.split('').reverse().join('');
                break;
              case 'wordCount':
                result = text.split(/\\s+/).length;
                break;
              case 'charFrequency':
                const freq = {};
                for (const char of text) {
                  freq[char] = (freq[char] || 0) + 1;
                }
                result = freq;
                break;
              default:
                result = text;
            }

            self.postMessage({
              operation: operation,
              result: result
            });
          };
        `,
        name: 'string-processor',
      });

      const testText = 'Hello World from React Native WebWorker Module!';

      worker.onmessage = async (event) => {
        const { operation, result } = event.data;
        addLog(`${operation}: ${JSON.stringify(result)}`, 'success');

        // Continue with next operation or finish
        if (operation === 'uppercase') {
          await worker.postMessage({ text: testText, operation: 'reverse' });
        } else if (operation === 'reverse') {
          await worker.postMessage({ text: testText, operation: 'wordCount' });
        } else if (operation === 'wordCount') {
          await worker.postMessage({
            text: testText,
            operation: 'charFrequency',
          });
        } else {
          // Done
          await worker.terminate();
          addLog('String processing worker terminated');
          setLoading(false);
        }
      };

      // Start processing
      await worker.postMessage({ text: testText, operation: 'uppercase' });
    } catch (error) {
      addLog(`Error: ${(error as Error).message}`, 'error');
      setLoading(false);
    }
  };

  const clearLogs = () => {
    setLogs([]);
  };

  const getLogStyle = (type: LogEntry['type']) => {
    switch (type) {
      case 'success':
        return styles.logSuccess;
      case 'error':
        return styles.logError;
      case 'warning':
        return styles.logWarning;
      default:
        return styles.logInfo;
    }
  };

  const renderExamplesTab = () => (
    <>
      <View style={styles.header}>
        <Text style={styles.title}>WebWorker Examples</Text>
        {loading && <ActivityIndicator size="small" color="#007AFF" />}
      </View>

      <ScrollView style={styles.buttonsContainer}>
        <TouchableOpacity
          style={styles.button}
          onPress={runSimpleWorker}
          disabled={loading}
        >
          <Text style={styles.buttonText}>1. Simple Computation</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={styles.button}
          onPress={runHeavyComputation}
          disabled={loading}
        >
          <Text style={styles.buttonText}>
            2. Heavy Computation (Fibonacci)
          </Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={styles.button}
          onPress={runWorkerPool}
          disabled={loading}
        >
          <Text style={styles.buttonText}>3. Worker Pool (Parallel)</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={styles.button}
          onPress={runEventWorker}
          disabled={loading}
        >
          <Text style={styles.buttonText}>4. Event-Driven Worker</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={styles.button}
          onPress={runStringProcessing}
          disabled={loading}
        >
          <Text style={styles.buttonText}>5. String Processing</Text>
        </TouchableOpacity>

        <TouchableOpacity
          style={[styles.button, styles.clearButton]}
          onPress={clearLogs}
        >
          <Text style={styles.buttonText}>Clear Logs</Text>
        </TouchableOpacity>
      </ScrollView>

      <View style={styles.logsContainer}>
        <Text style={styles.logsTitle}>Logs:</Text>
        <ScrollView style={styles.logs}>
          {logs.map((log, index) => (
            <View key={index} style={styles.logItem}>
              <Text style={[styles.logText, getLogStyle(log.type)]}>
                {log.message}
              </Text>
            </View>
          ))}
        </ScrollView>
      </View>
    </>
  );

  return (
    <SafeAreaView style={styles.container}>
      {/* Tab Bar */}
      <View style={styles.tabBar}>
        <TouchableOpacity
          style={[styles.tab, activeTab === 'examples' && styles.activeTab]}
          onPress={() => setActiveTab('examples')}
        >
          <Text
            style={[
              styles.tabText,
              activeTab === 'examples' && styles.activeTabText,
            ]}
          >
            Examples
          </Text>
        </TouchableOpacity>
        <TouchableOpacity
          style={[styles.tab, activeTab === 'parallel' && styles.activeTab]}
          onPress={() => setActiveTab('parallel')}
        >
          <Text
            style={[
              styles.tabText,
              activeTab === 'parallel' && styles.activeTabText,
            ]}
          >
            Parallel Demo
          </Text>
        </TouchableOpacity>
      </View>

      {/* Tab Content */}
      {activeTab === 'examples' ? renderExamplesTab() : <ParallelDemo />}
    </SafeAreaView>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  tabBar: {
    flexDirection: 'row',
    backgroundColor: '#fff',
    borderBottomWidth: 1,
    borderBottomColor: '#e0e0e0',
  },
  tab: {
    flex: 1,
    paddingVertical: 14,
    alignItems: 'center',
    borderBottomWidth: 2,
    borderBottomColor: 'transparent',
  },
  activeTab: {
    borderBottomColor: '#007AFF',
  },
  tabText: {
    fontSize: 16,
    fontWeight: '500',
    color: '#888',
  },
  activeTabText: {
    color: '#007AFF',
    fontWeight: '600',
  },
  header: {
    padding: 20,
    backgroundColor: '#fff',
    borderBottomWidth: 1,
    borderBottomColor: '#e0e0e0',
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
  },
  buttonsContainer: {
    flex: 1,
    padding: 10,
  },
  button: {
    backgroundColor: '#007AFF',
    padding: 15,
    borderRadius: 8,
    marginBottom: 10,
  },
  clearButton: {
    backgroundColor: '#FF3B30',
  },
  buttonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
    textAlign: 'center',
  },
  logsContainer: {
    height: 300,
    backgroundColor: '#fff',
    borderTopWidth: 1,
    borderTopColor: '#e0e0e0',
  },
  logsTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    padding: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#e0e0e0',
  },
  logs: {
    flex: 1,
  },
  logItem: {
    padding: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#f0f0f0',
  },
  logText: {
    fontSize: 14,
    fontFamily: 'monospace',
  },
  logInfo: {
    color: '#333',
  },
  logSuccess: {
    color: '#34C759',
  },
  logError: {
    color: '#FF3B30',
  },
  logWarning: {
    color: '#FF9500',
  },
});

export default App;
