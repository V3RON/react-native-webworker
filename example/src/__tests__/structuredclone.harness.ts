/**
 * Structured Clone E2E Tests
 *
 * These tests verify the full round-trip of the Structured Clone implementation:
 * TypeScript Encoder -> base64 -> Native Bridge -> C++ Deserialize -> Worker JS
 * -> Worker postMessage -> C++ Serialize -> Native Bridge -> base64 -> TypeScript Decoder
 */
import { describe, test, expect } from 'react-native-harness';
import { Worker, DataCloneError } from 'react-native-webworker';
import { echoWorkerScript } from './echoWorkerScript';

/**
 * Helper to send a message to a worker and wait for the echo response
 */
async function roundTrip<T>(value: T): Promise<unknown> {
  const worker = new Worker({ script: echoWorkerScript });

  try {
    const received = await new Promise<unknown>((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Timeout waiting for worker response'));
      }, 5000);

      worker.onmessage = (e) => {
        clearTimeout(timeout);
        resolve(e.data);
      };

      worker.onerror = (error) => {
        clearTimeout(timeout);
        reject(error);
      };

      worker.postMessage(value);
    });

    return received;
  } finally {
    await worker.terminate();
  }
}

/**
 * Helper to compare ArrayBuffers byte-by-byte
 */
function arrayBuffersEqual(a: ArrayBuffer, b: ArrayBuffer): boolean {
  if (a.byteLength !== b.byteLength) return false;
  const viewA = new Uint8Array(a);
  const viewB = new Uint8Array(b);
  for (let i = 0; i < viewA.length; i++) {
    if (viewA[i] !== viewB[i]) return false;
  }
  return true;
}

/**
 * Helper to compare TypedArrays
 */
function typedArraysEqual(a: ArrayBufferView, b: ArrayBufferView): boolean {
  if (a.byteLength !== b.byteLength) return false;
  const viewA = new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
  const viewB = new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
  for (let i = 0; i < viewA.length; i++) {
    if (viewA[i] !== viewB[i]) return false;
  }
  return true;
}

// =============================================================================
// Primitive Tests
// =============================================================================

describe('Structured Clone - Primitives', () => {
  test('should round-trip undefined', async () => {
    const result = await roundTrip(undefined);
    expect(result).toBe(undefined);
  });

  test('should round-trip null', async () => {
    const result = await roundTrip(null);
    expect(result).toBe(null);
  });

  test('should round-trip true', async () => {
    const result = await roundTrip(true);
    expect(result).toBe(true);
  });

  test('should round-trip false', async () => {
    const result = await roundTrip(false);
    expect(result).toBe(false);
  });

  test('should round-trip positive integer', async () => {
    const result = await roundTrip(42);
    expect(result).toBe(42);
  });

  test('should round-trip negative integer', async () => {
    const result = await roundTrip(-42);
    expect(result).toBe(-42);
  });

  test('should round-trip zero', async () => {
    const result = await roundTrip(0);
    expect(result).toBe(0);
  });

  test('should round-trip large integer', async () => {
    const result = await roundTrip(2147483647);
    expect(result).toBe(2147483647);
  });

  test('should round-trip decimal number', async () => {
    const result = await roundTrip(3.14159);
    expect(result).toBe(3.14159);
  });

  test('should round-trip Infinity', async () => {
    const result = await roundTrip(Infinity);
    expect(result).toBe(Infinity);
  });

  test('should round-trip -Infinity', async () => {
    const result = await roundTrip(-Infinity);
    expect(result).toBe(-Infinity);
  });

  test('should round-trip NaN', async () => {
    const result = await roundTrip(NaN);
    expect(Number.isNaN(result)).toBe(true);
  });

  test('should round-trip empty string', async () => {
    const result = await roundTrip('');
    expect(result).toBe('');
  });

  test('should round-trip ASCII string', async () => {
    const result = await roundTrip('Hello, World!');
    expect(result).toBe('Hello, World!');
  });

  test('should round-trip Unicode string', async () => {
    const result = await roundTrip('Привет мир! 你好世界!');
    expect(result).toBe('Привет мир! 你好世界!');
  });

  test('should round-trip emoji string', async () => {
    const result = await roundTrip('Hello 👋 World 🌍');
    expect(result).toBe('Hello 👋 World 🌍');
  });

  test('should round-trip BigInt', async () => {
    const result = await roundTrip(BigInt('9007199254740993'));
    expect(typeof result).toBe('bigint');
    expect(result).toBe(BigInt('9007199254740993'));
  });

  test('should round-trip negative BigInt', async () => {
    const result = await roundTrip(BigInt('-12345678901234567890'));
    expect(typeof result).toBe('bigint');
    expect(result).toBe(BigInt('-12345678901234567890'));
  });
});

// =============================================================================
// Object and Array Tests
// =============================================================================

describe('Structured Clone - Objects and Arrays', () => {
  test('should round-trip empty object', async () => {
    const result = await roundTrip({});
    expect(typeof result).toBe('object');
    expect(result).not.toBe(null);
    expect(Object.keys(result as object).length).toBe(0);
  });

  test('should round-trip object with string keys', async () => {
    const original = { name: 'John', age: 30, active: true };
    const result = (await roundTrip(original)) as typeof original;
    expect(result.name).toBe('John');
    expect(result.age).toBe(30);
    expect(result.active).toBe(true);
  });

  test('should round-trip nested objects', async () => {
    const original = {
      user: {
        name: 'John',
        address: {
          city: 'NYC',
          zip: '10001',
        },
      },
    };
    const result = (await roundTrip(original)) as typeof original;
    expect(result.user.name).toBe('John');
    expect(result.user.address.city).toBe('NYC');
    expect(result.user.address.zip).toBe('10001');
  });

  test('should round-trip empty array', async () => {
    const result = await roundTrip([]);
    expect(Array.isArray(result)).toBe(true);
    expect((result as unknown[]).length).toBe(0);
  });

  test('should round-trip array with primitives', async () => {
    const original = [1, 'two', true, null, undefined];
    const result = (await roundTrip(original)) as typeof original;
    expect(result[0]).toBe(1);
    expect(result[1]).toBe('two');
    expect(result[2]).toBe(true);
    expect(result[3]).toBe(null);
    expect(result[4]).toBe(undefined);
  });

  test('should round-trip array with mixed types', async () => {
    const original = [42, 'hello', { nested: true }, [1, 2, 3]];
    const result = (await roundTrip(original)) as unknown[];
    expect(result[0]).toBe(42);
    expect(result[1]).toBe('hello');
    expect((result[2] as { nested: boolean }).nested).toBe(true);
    expect((result[3] as number[]).length).toBe(3);
  });

  test('should round-trip nested arrays', async () => {
    const original = [
      [1, 2],
      [3, 4],
      [
        [5, 6],
        [7, 8],
      ],
    ];
    const result = (await roundTrip(original)) as number[][][];
    expect(result[0]![0]).toBe(1);
    expect(result[2]![1]![1]).toBe(8);
  });

  test('should round-trip sparse array (holes become undefined)', async () => {
    // Note: Per HTML5 spec, sparse arrays should preserve holes.
    // This implementation converts holes to undefined values.
    const original: (number | undefined)[] = [];
    original[0] = 1;
    original[5] = 5;
    original[10] = 10;

    const result = (await roundTrip(original)) as (number | undefined)[];
    expect(result.length).toBe(11);
    expect(result[0]).toBe(1);
    expect(result[5]).toBe(5);
    expect(result[10]).toBe(10);
    // Holes are converted to undefined (not preserved as holes)
    expect(result[1]).toBe(undefined);
    expect(result[6]).toBe(undefined);
  });
});

// =============================================================================
// Special Object Tests (Date, RegExp, Map, Set)
// =============================================================================

describe('Structured Clone - Date', () => {
  test('should round-trip current date', async () => {
    const original = new Date();
    const result = await roundTrip(original);
    expect(result instanceof Date).toBe(true);
    expect((result as Date).getTime()).toBe(original.getTime());
  });

  test('should round-trip epoch date', async () => {
    const original = new Date(0);
    const result = await roundTrip(original);
    expect(result instanceof Date).toBe(true);
    expect((result as Date).getTime()).toBe(0);
  });

  test('should round-trip future date', async () => {
    const original = new Date('2099-12-31T23:59:59.999Z');
    const result = await roundTrip(original);
    expect(result instanceof Date).toBe(true);
    expect((result as Date).getTime()).toBe(original.getTime());
  });

  test('should round-trip date in object', async () => {
    const original = { created: new Date('2024-01-15T10:30:00Z') };
    const result = (await roundTrip(original)) as typeof original;
    expect(result.created instanceof Date).toBe(true);
    expect(result.created.getTime()).toBe(original.created.getTime());
  });
});

describe('Structured Clone - RegExp', () => {
  test('should round-trip simple regexp', async () => {
    const original = /hello/;
    const result = await roundTrip(original);
    expect(result instanceof RegExp).toBe(true);
    expect((result as RegExp).source).toBe('hello');
    expect((result as RegExp).flags).toBe('');
  });

  test('should round-trip regexp with flags', async () => {
    const original = /pattern/gim;
    const result = await roundTrip(original);
    expect(result instanceof RegExp).toBe(true);
    expect((result as RegExp).source).toBe('pattern');
    expect((result as RegExp).flags).toBe('gim');
  });

  test('should round-trip complex regexp', async () => {
    const original = /^[a-z]+\d{2,4}$/iu;
    const result = await roundTrip(original);
    expect(result instanceof RegExp).toBe(true);
    expect((result as RegExp).source).toBe('^[a-z]+\\d{2,4}$');
    expect((result as RegExp).flags).toBe('iu');
  });
});

describe('Structured Clone - Map', () => {
  test('should round-trip empty Map', async () => {
    const original = new Map();
    const result = await roundTrip(original);
    expect(result instanceof Map).toBe(true);
    expect((result as Map<unknown, unknown>).size).toBe(0);
  });

  test('should round-trip Map with string keys', async () => {
    const original = new Map([
      ['a', 1],
      ['b', 2],
      ['c', 3],
    ]);
    const result = (await roundTrip(original)) as Map<string, number>;
    expect(result instanceof Map).toBe(true);
    expect(result.size).toBe(3);
    expect(result.get('a')).toBe(1);
    expect(result.get('b')).toBe(2);
    expect(result.get('c')).toBe(3);
  });

  test('should round-trip Map with mixed value types', async () => {
    const original = new Map<string, unknown>([
      ['string', 'hello'],
      ['number', 42],
      ['boolean', true],
      ['null', null],
      ['array', [1, 2, 3]],
    ]);
    const result = (await roundTrip(original)) as Map<string, unknown>;
    expect(result.get('string')).toBe('hello');
    expect(result.get('number')).toBe(42);
    expect(result.get('boolean')).toBe(true);
    expect(result.get('null')).toBe(null);
    expect((result.get('array') as number[]).length).toBe(3);
  });

  test('should round-trip nested Map', async () => {
    const inner = new Map([['inner', 'value']]);
    const original = new Map([['outer', inner]]);
    const result = (await roundTrip(original)) as Map<
      string,
      Map<string, string>
    >;
    expect(result.get('outer') instanceof Map).toBe(true);
    expect(result.get('outer')?.get('inner')).toBe('value');
  });
});

describe('Structured Clone - Set', () => {
  test('should round-trip empty Set', async () => {
    const original = new Set();
    const result = await roundTrip(original);
    expect(result instanceof Set).toBe(true);
    expect((result as Set<unknown>).size).toBe(0);
  });

  test('should round-trip Set with primitives', async () => {
    const original = new Set([1, 2, 3, 'hello', true]);
    const result = (await roundTrip(original)) as Set<unknown>;
    expect(result instanceof Set).toBe(true);
    expect(result.size).toBe(5);
    expect(result.has(1)).toBe(true);
    expect(result.has(2)).toBe(true);
    expect(result.has(3)).toBe(true);
    expect(result.has('hello')).toBe(true);
    expect(result.has(true)).toBe(true);
  });

  test('should round-trip Set with objects', async () => {
    const original = new Set([{ a: 1 }, { b: 2 }]);
    const result = (await roundTrip(original)) as Set<{
      a?: number;
      b?: number;
    }>;
    expect(result instanceof Set).toBe(true);
    expect(result.size).toBe(2);
    const values = Array.from(result);
    expect(values.some((v) => v.a === 1)).toBe(true);
    expect(values.some((v) => v.b === 2)).toBe(true);
  });
});

// =============================================================================
// Error Type Tests
// =============================================================================

describe('Structured Clone - Error Types', () => {
  test('should round-trip Error', async () => {
    const original = new Error('Test error message');
    const result = await roundTrip(original);
    expect(result instanceof Error).toBe(true);
    expect((result as Error).message).toBe('Test error message');
  });

  test('should round-trip TypeError', async () => {
    const original = new TypeError('Type error message');
    const result = await roundTrip(original);
    expect(result instanceof TypeError).toBe(true);
    expect((result as TypeError).message).toBe('Type error message');
  });

  test('should round-trip RangeError', async () => {
    const original = new RangeError('Range error message');
    const result = await roundTrip(original);
    expect(result instanceof RangeError).toBe(true);
    expect((result as RangeError).message).toBe('Range error message');
  });

  test('should round-trip ReferenceError', async () => {
    const original = new ReferenceError('Reference error message');
    const result = await roundTrip(original);
    expect(result instanceof ReferenceError).toBe(true);
    expect((result as ReferenceError).message).toBe('Reference error message');
  });

  test('should round-trip SyntaxError', async () => {
    const original = new SyntaxError('Syntax error message');
    const result = await roundTrip(original);
    expect(result instanceof SyntaxError).toBe(true);
    expect((result as SyntaxError).message).toBe('Syntax error message');
  });

  test('should round-trip EvalError', async () => {
    const original = new EvalError('Eval error message');
    const result = await roundTrip(original);
    expect(result instanceof EvalError).toBe(true);
    expect((result as EvalError).message).toBe('Eval error message');
  });

  test('should round-trip URIError', async () => {
    const original = new URIError('URI error message');
    const result = await roundTrip(original);
    expect(result instanceof URIError).toBe(true);
    expect((result as URIError).message).toBe('URI error message');
  });

  test('should round-trip Error in object', async () => {
    const original = { error: new TypeError('Nested error') };
    const result = (await roundTrip(original)) as typeof original;
    expect(result.error instanceof TypeError).toBe(true);
    expect(result.error.message).toBe('Nested error');
  });
});

// =============================================================================
// Binary Data Tests
// =============================================================================

describe('Structured Clone - ArrayBuffer', () => {
  test('should round-trip empty ArrayBuffer', async () => {
    const original = new ArrayBuffer(0);
    const result = await roundTrip(original);
    expect(result instanceof ArrayBuffer).toBe(true);
    expect((result as ArrayBuffer).byteLength).toBe(0);
  });

  test('should round-trip small ArrayBuffer', async () => {
    const original = new ArrayBuffer(8);
    const view = new Uint8Array(original);
    view.set([1, 2, 3, 4, 5, 6, 7, 8]);

    const result = (await roundTrip(original)) as ArrayBuffer;
    expect(result instanceof ArrayBuffer).toBe(true);
    expect(arrayBuffersEqual(original, result)).toBe(true);
  });

  test('should round-trip larger ArrayBuffer', async () => {
    const original = new ArrayBuffer(1024);
    const view = new Uint8Array(original);
    for (let i = 0; i < view.length; i++) {
      view[i] = i % 256;
    }

    const result = (await roundTrip(original)) as ArrayBuffer;
    expect(result instanceof ArrayBuffer).toBe(true);
    expect(arrayBuffersEqual(original, result)).toBe(true);
  });
});

describe('Structured Clone - TypedArrays', () => {
  test('should round-trip Uint8Array', async () => {
    const original = new Uint8Array([0, 127, 255]);
    const result = await roundTrip(original);
    expect(result instanceof Uint8Array).toBe(true);
    expect(typedArraysEqual(original, result as Uint8Array)).toBe(true);
  });

  test('should round-trip Int8Array', async () => {
    const original = new Int8Array([-128, 0, 127]);
    const result = await roundTrip(original);
    expect(result instanceof Int8Array).toBe(true);
    expect(typedArraysEqual(original, result as Int8Array)).toBe(true);
  });

  test('should round-trip Uint16Array', async () => {
    const original = new Uint16Array([0, 32767, 65535]);
    const result = await roundTrip(original);
    expect(result instanceof Uint16Array).toBe(true);
    expect(typedArraysEqual(original, result as Uint16Array)).toBe(true);
  });

  test('should round-trip Int16Array', async () => {
    const original = new Int16Array([-32768, 0, 32767]);
    const result = await roundTrip(original);
    expect(result instanceof Int16Array).toBe(true);
    expect(typedArraysEqual(original, result as Int16Array)).toBe(true);
  });

  test('should round-trip Uint32Array', async () => {
    const original = new Uint32Array([0, 2147483647, 4294967295]);
    const result = await roundTrip(original);
    expect(result instanceof Uint32Array).toBe(true);
    expect(typedArraysEqual(original, result as Uint32Array)).toBe(true);
  });

  test('should round-trip Int32Array', async () => {
    const original = new Int32Array([-2147483648, 0, 2147483647]);
    const result = await roundTrip(original);
    expect(result instanceof Int32Array).toBe(true);
    expect(typedArraysEqual(original, result as Int32Array)).toBe(true);
  });

  test('should round-trip Float32Array', async () => {
    const original = new Float32Array([0, 3.14, -2.5, Infinity, -Infinity]);
    const result = (await roundTrip(original)) as Float32Array;
    expect(result instanceof Float32Array).toBe(true);
    expect(result.length).toBe(original.length);
    for (let i = 0; i < original.length; i++) {
      if (Number.isNaN(original[i])) {
        expect(Number.isNaN(result[i])).toBe(true);
      } else {
        expect(result[i]).toBe(original[i]);
      }
    }
  });

  test('should round-trip Float64Array', async () => {
    const original = new Float64Array([
      0,
      Math.PI,
      -Math.E,
      Infinity,
      -Infinity,
      NaN,
    ]);
    const result = (await roundTrip(original)) as Float64Array;
    expect(result instanceof Float64Array).toBe(true);
    expect(result.length).toBe(original.length);
    for (let i = 0; i < original.length; i++) {
      if (Number.isNaN(original[i])) {
        expect(Number.isNaN(result[i])).toBe(true);
      } else {
        expect(result[i]).toBe(original[i]);
      }
    }
  });

  test('should round-trip Uint8ClampedArray', async () => {
    const original = new Uint8ClampedArray([0, 127, 255]);
    const result = await roundTrip(original);
    expect(result instanceof Uint8ClampedArray).toBe(true);
    expect(typedArraysEqual(original, result as Uint8ClampedArray)).toBe(true);
  });

  test('should round-trip BigInt64Array', async () => {
    const original = new BigInt64Array([
      BigInt('-9223372036854775808'),
      BigInt('0'),
      BigInt('9223372036854775807'),
    ]);
    const result = (await roundTrip(original)) as BigInt64Array;
    expect(result instanceof BigInt64Array).toBe(true);
    expect(result.length).toBe(3);
    expect(result[0]).toBe(BigInt('-9223372036854775808'));
    expect(result[1]).toBe(BigInt('0'));
    expect(result[2]).toBe(BigInt('9223372036854775807'));
  });

  test('should round-trip BigUint64Array', async () => {
    const original = new BigUint64Array([
      BigInt('0'),
      BigInt('9223372036854775807'),
      BigInt('18446744073709551615'),
    ]);
    const result = (await roundTrip(original)) as BigUint64Array;
    expect(result instanceof BigUint64Array).toBe(true);
    expect(result.length).toBe(3);
    expect(result[0]).toBe(BigInt('0'));
    expect(result[1]).toBe(BigInt('9223372036854775807'));
    expect(result[2]).toBe(BigInt('18446744073709551615'));
  });
});

describe('Structured Clone - DataView', () => {
  test('should round-trip DataView', async () => {
    const buffer = new ArrayBuffer(16);
    const original = new DataView(buffer);
    original.setInt32(0, 42, true);
    original.setFloat64(4, Math.PI, true);

    const result = (await roundTrip(original)) as DataView;
    expect(result instanceof DataView).toBe(true);
    expect(result.byteLength).toBe(16);
    expect(result.getInt32(0, true)).toBe(42);
    expect(result.getFloat64(4, true)).toBeCloseTo(Math.PI, 10);
  });

  test('should round-trip DataView with offset', async () => {
    const buffer = new ArrayBuffer(32);
    const original = new DataView(buffer, 8, 16);
    original.setUint32(0, 0xdeadbeef, true);
    original.setUint32(12, 0xcafebabe, true);

    const result = (await roundTrip(original)) as DataView;
    expect(result instanceof DataView).toBe(true);
    expect(result.byteOffset).toBe(8);
    expect(result.byteLength).toBe(16);
    expect(result.getUint32(0, true)).toBe(0xdeadbeef);
    expect(result.getUint32(12, true)).toBe(0xcafebabe);
  });
});

// =============================================================================
// Circular Reference Tests
// =============================================================================

describe('Structured Clone - Circular References', () => {
  test('should round-trip self-referencing object', async () => {
    const original: { self?: unknown; value: number } = { value: 42 };
    original.self = original;

    const result = (await roundTrip(original)) as typeof original;
    expect(result.value).toBe(42);
    expect(result.self).toBe(result);
  });

  test('should round-trip mutually referencing objects', async () => {
    const a: { value: string; ref?: unknown } = { value: 'a' };
    const b: { value: string; ref?: unknown } = { value: 'b' };
    a.ref = b;
    b.ref = a;

    const result = (await roundTrip(a)) as typeof a;
    expect(result.value).toBe('a');
    expect((result.ref as typeof b).value).toBe('b');
    expect((result.ref as typeof b).ref).toBe(result);
  });

  test('should round-trip circular array', async () => {
    const original: unknown[] = [1, 2, 3];
    original.push(original);

    const result = (await roundTrip(original)) as unknown[];
    expect(result[0]).toBe(1);
    expect(result[1]).toBe(2);
    expect(result[2]).toBe(3);
    expect(result[3]).toBe(result);
  });

  test('should round-trip deeply nested circular reference', async () => {
    const root: {
      level1?: { level2?: { level3?: { backToRoot?: unknown } } };
    } = {};
    root.level1 = {};
    root.level1.level2 = {};
    root.level1.level2.level3 = {};
    root.level1.level2.level3.backToRoot = root;

    const result = (await roundTrip(root)) as typeof root;
    expect(result.level1?.level2?.level3?.backToRoot).toBe(result);
  });

  test('should handle multiple references to same object', async () => {
    const shared = { value: 'shared' };
    const original = { a: shared, b: shared, c: shared };

    const result = (await roundTrip(original)) as typeof original;
    expect(result.a.value).toBe('shared');
    expect(result.a).toBe(result.b);
    expect(result.b).toBe(result.c);
  });
});

// =============================================================================
// DataCloneError Tests
// =============================================================================

describe('Structured Clone - DataCloneError', () => {
  test('should throw DataCloneError for Function', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage(() => {});
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });

  test('should throw DataCloneError for Symbol', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage(Symbol('test'));
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });

  test('should throw DataCloneError for WeakMap', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage(new WeakMap());
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });

  test('should throw DataCloneError for WeakSet', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage(new WeakSet());
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });

  test('should throw DataCloneError for Promise', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage(Promise.resolve());
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });

  test('should throw DataCloneError for Function nested in object', async () => {
    const worker = new Worker({ script: echoWorkerScript });

    try {
      await expect(async () => {
        await worker.postMessage({ callback: () => {} });
      }).rejects.toThrow(DataCloneError);
    } finally {
      await worker.terminate();
    }
  });
});

// =============================================================================
// Complex Integration Tests
// =============================================================================

describe('Structured Clone - Complex Integration', () => {
  test('should round-trip complex nested structure', async () => {
    const original = {
      metadata: {
        created: new Date('2024-01-15T10:30:00Z'),
        pattern: /^\d{4}-\d{2}-\d{2}$/,
        tags: new Set(['important', 'processed']),
      },
      data: {
        items: new Map([
          ['item1', { value: 100, active: true }],
          ['item2', { value: 200, active: false }],
        ]),
        buffer: new Uint8Array([1, 2, 3, 4, 5]),
      },
      errors: [new TypeError('Invalid type'), new RangeError('Out of bounds')],
    };

    const result = (await roundTrip(original)) as typeof original;

    // Verify metadata
    expect(result.metadata.created instanceof Date).toBe(true);
    expect(result.metadata.created.getTime()).toBe(
      original.metadata.created.getTime()
    );
    expect(result.metadata.pattern instanceof RegExp).toBe(true);
    expect(result.metadata.pattern.source).toBe(
      original.metadata.pattern.source
    );
    expect(result.metadata.tags instanceof Set).toBe(true);
    expect(result.metadata.tags.has('important')).toBe(true);

    // Verify data
    expect(result.data.items instanceof Map).toBe(true);
    expect(result.data.items.get('item1')?.value).toBe(100);
    expect(result.data.buffer instanceof Uint8Array).toBe(true);
    expect(typedArraysEqual(original.data.buffer, result.data.buffer)).toBe(
      true
    );

    // Verify errors
    expect(result.errors[0] instanceof TypeError).toBe(true);
    expect(result.errors[1] instanceof RangeError).toBe(true);
  });
});
