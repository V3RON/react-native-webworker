/**
 * StructuredCloneEncoder - Encodes JavaScript values to binary format
 *
 * This encoder produces the same binary format as the C++ StructuredCloneWriter,
 * allowing JavaScript values to be sent to workers with full type fidelity.
 *
 * Supported types:
 * - Primitives: undefined, null, boolean, number, string
 * - Objects: plain objects, arrays
 * - Special objects: Date, RegExp, Map, Set, Error types
 * - Binary data: ArrayBuffer, TypedArrays, DataView
 * - Circular references
 *
 * Throws DataCloneError for non-serializable types (Function, Symbol, WeakMap, etc.)
 */

// Type tags from StructuredCloneTypes.h
const enum CloneType {
  Undefined = 0x00,
  Null = 0x01,
  BoolTrue = 0x02,
  BoolFalse = 0x03,
  Int32 = 0x04,
  Double = 0x05,
  BigInt = 0x06,
  String = 0x07,

  Object = 0x10,
  Array = 0x11,
  Date = 0x12,
  RegExp = 0x13,
  Map = 0x14,
  Set = 0x15,

  Error = 0x16,
  EvalError = 0x17,
  RangeError = 0x18,
  ReferenceError = 0x19,
  SyntaxError = 0x1a,
  TypeError = 0x1b,
  URIError = 0x1c,

  ArrayBuffer = 0x20,
  DataView = 0x21,
  Int8Array = 0x22,
  Uint8Array = 0x23,
  Uint8ClampedArray = 0x24,
  Int16Array = 0x25,
  Uint16Array = 0x26,
  Int32Array = 0x27,
  Uint32Array = 0x28,
  Float32Array = 0x29,
  Float64Array = 0x2a,
  BigInt64Array = 0x2b,
  BigUint64Array = 0x2c,

  ObjectRef = 0xf0,
}

export class DataCloneError extends Error {
  constructor(message: string) {
    super(`DataCloneError: ${message}`);
    this.name = 'DataCloneError';
  }
}

class WriteBuffer {
  private chunks: Uint8Array[] = [];
  private currentChunk: Uint8Array;
  private offset: number = 0;
  private readonly chunkSize = 4096;

  constructor() {
    this.currentChunk = new Uint8Array(this.chunkSize);
  }

  private ensureSpace(bytes: number): void {
    if (this.offset + bytes > this.currentChunk.length) {
      // Save current chunk and create new one
      this.chunks.push(this.currentChunk.subarray(0, this.offset));
      this.currentChunk = new Uint8Array(Math.max(this.chunkSize, bytes));
      this.offset = 0;
    }
  }

  writeU8(value: number): void {
    this.ensureSpace(1);
    this.currentChunk[this.offset++] = value & 0xff;
  }

  writeU32(value: number): void {
    this.ensureSpace(4);
    this.currentChunk[this.offset++] = value & 0xff;
    this.currentChunk[this.offset++] = (value >> 8) & 0xff;
    this.currentChunk[this.offset++] = (value >> 16) & 0xff;
    this.currentChunk[this.offset++] = (value >> 24) & 0xff;
  }

  writeI32(value: number): void {
    this.writeU32(value >>> 0);
  }

  writeDouble(value: number): void {
    this.ensureSpace(8);
    const view = new DataView(
      this.currentChunk.buffer,
      this.currentChunk.byteOffset + this.offset,
      8
    );
    view.setFloat64(0, value, true); // Little-endian
    this.offset += 8;
  }

  writeString(str: string): void {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(str);
    this.writeU32(bytes.length);
    this.writeBytes(bytes);
  }

  writeBytes(bytes: Uint8Array): void {
    this.ensureSpace(bytes.length);
    this.currentChunk.set(bytes, this.offset);
    this.offset += bytes.length;
  }

  toUint8Array(): Uint8Array {
    // Calculate total length
    let totalLength = this.offset;
    for (const chunk of this.chunks) {
      totalLength += chunk.length;
    }

    // Combine all chunks
    const result = new Uint8Array(totalLength);
    let pos = 0;
    for (const chunk of this.chunks) {
      result.set(chunk, pos);
      pos += chunk.length;
    }
    result.set(this.currentChunk.subarray(0, this.offset), pos);

    return result;
  }
}

export function encodeStructuredClone(value: unknown): Uint8Array {
  const buffer = new WriteBuffer();
  const memoryMap = new Map<object, number>();
  let nextRefId = 0;

  function writeValue(val: unknown): void {
    if (val === undefined) {
      buffer.writeU8(CloneType.Undefined);
      return;
    }

    if (val === null) {
      buffer.writeU8(CloneType.Null);
      return;
    }

    if (typeof val === 'boolean') {
      buffer.writeU8(val ? CloneType.BoolTrue : CloneType.BoolFalse);
      return;
    }

    if (typeof val === 'number') {
      // Check if it can be represented as int32
      if (
        Number.isFinite(val) &&
        val >= -2147483648 &&
        val <= 2147483647 &&
        val === Math.floor(val)
      ) {
        buffer.writeU8(CloneType.Int32);
        buffer.writeI32(val);
      } else {
        buffer.writeU8(CloneType.Double);
        buffer.writeDouble(val);
      }
      return;
    }

    if (typeof val === 'string') {
      buffer.writeU8(CloneType.String);
      buffer.writeString(val);
      return;
    }

    if (typeof val === 'symbol') {
      throw new DataCloneError('Symbol cannot be cloned');
    }

    if (typeof val === 'function') {
      throw new DataCloneError('Function cannot be cloned');
    }

    if (typeof val === 'object') {
      // Check for circular reference
      const existingRef = memoryMap.get(val);
      if (existingRef !== undefined) {
        buffer.writeU8(CloneType.ObjectRef);
        buffer.writeU32(existingRef);
        return;
      }

      // Handle different object types
      const toString = Object.prototype.toString.call(val);

      if (val instanceof Date) {
        buffer.writeU8(CloneType.Date);
        buffer.writeDouble(val.getTime());
        return;
      }

      if (val instanceof RegExp) {
        buffer.writeU8(CloneType.RegExp);
        buffer.writeString(val.source);
        buffer.writeString(val.flags);
        return;
      }

      if (val instanceof Map) {
        buffer.writeU8(CloneType.Map);
        const refId = nextRefId++;
        memoryMap.set(val, refId);

        buffer.writeU32(val.size);
        val.forEach((v, k) => {
          writeValue(k);
          writeValue(v);
        });
        return;
      }

      if (val instanceof Set) {
        buffer.writeU8(CloneType.Set);
        const refId = nextRefId++;
        memoryMap.set(val, refId);

        buffer.writeU32(val.size);
        val.forEach((v) => {
          writeValue(v);
        });
        return;
      }

      if (val instanceof ArrayBuffer) {
        buffer.writeU8(CloneType.ArrayBuffer);
        const bytes = new Uint8Array(val);
        buffer.writeU32(bytes.length);
        buffer.writeBytes(bytes);
        return;
      }

      if (ArrayBuffer.isView(val)) {
        if (val instanceof DataView) {
          buffer.writeU8(CloneType.DataView);
        } else if (val instanceof Int8Array) {
          buffer.writeU8(CloneType.Int8Array);
        } else if (val instanceof Uint8Array) {
          buffer.writeU8(CloneType.Uint8Array);
        } else if (val instanceof Uint8ClampedArray) {
          buffer.writeU8(CloneType.Uint8ClampedArray);
        } else if (val instanceof Int16Array) {
          buffer.writeU8(CloneType.Int16Array);
        } else if (val instanceof Uint16Array) {
          buffer.writeU8(CloneType.Uint16Array);
        } else if (val instanceof Int32Array) {
          buffer.writeU8(CloneType.Int32Array);
        } else if (val instanceof Uint32Array) {
          buffer.writeU8(CloneType.Uint32Array);
        } else if (val instanceof Float32Array) {
          buffer.writeU8(CloneType.Float32Array);
        } else if (val instanceof Float64Array) {
          buffer.writeU8(CloneType.Float64Array);
        } else if (val instanceof BigInt64Array) {
          buffer.writeU8(CloneType.BigInt64Array);
        } else if (val instanceof BigUint64Array) {
          buffer.writeU8(CloneType.BigUint64Array);
        }

        // Write underlying buffer
        const bytes = new Uint8Array(val.buffer);
        buffer.writeU32(bytes.length);
        buffer.writeBytes(bytes);

        // Write offset and length
        buffer.writeU32(val.byteOffset);
        if (val instanceof DataView) {
          buffer.writeU32(val.byteLength);
        } else {
          buffer.writeU32((val as Int8Array).length);
        }
        return;
      }

      if (val instanceof Error) {
        let cloneType = CloneType.Error;
        if (val instanceof EvalError) cloneType = CloneType.EvalError;
        else if (val instanceof RangeError) cloneType = CloneType.RangeError;
        else if (val instanceof ReferenceError)
          cloneType = CloneType.ReferenceError;
        else if (val instanceof SyntaxError) cloneType = CloneType.SyntaxError;
        else if (val instanceof TypeError) cloneType = CloneType.TypeError;
        else if (val instanceof URIError) cloneType = CloneType.URIError;

        buffer.writeU8(cloneType);
        buffer.writeString(val.name);
        buffer.writeString(val.message);
        return;
      }

      if (toString === '[object WeakMap]') {
        throw new DataCloneError('WeakMap cannot be cloned');
      }

      if (toString === '[object WeakSet]') {
        throw new DataCloneError('WeakSet cannot be cloned');
      }

      if (toString === '[object Promise]') {
        throw new DataCloneError('Promise cannot be cloned');
      }

      if (Array.isArray(val)) {
        buffer.writeU8(CloneType.Array);
        const refId = nextRefId++;
        memoryMap.set(val, refId);

        buffer.writeU32(val.length);
        for (let i = 0; i < val.length; i++) {
          writeValue(val[i]);
        }
        return;
      }

      // Plain object
      buffer.writeU8(CloneType.Object);
      const refId = nextRefId++;
      memoryMap.set(val, refId);

      const keys = Object.keys(val);
      buffer.writeU32(keys.length);
      for (const key of keys) {
        buffer.writeString(key);
        writeValue((val as Record<string, unknown>)[key]);
      }
      return;
    }

    throw new DataCloneError(`Unknown value type: ${typeof val}`);
  }

  writeValue(value);
  return buffer.toUint8Array();
}
