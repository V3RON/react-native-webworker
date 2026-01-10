/**
 * StructuredCloneDecoder - Decodes binary data from the structured clone algorithm
 *
 * This decoder can parse the binary format produced by the C++ StructuredCloneWriter
 * and reconstruct JavaScript values including:
 * - Primitives: undefined, null, boolean, number, string
 * - Objects: plain objects, arrays
 * - Special objects: Date, RegExp, Map, Set, Error types
 * - Binary data: ArrayBuffer, TypedArrays, DataView
 * - Circular references
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

class ReadBuffer {
  private data: Uint8Array;
  private offset: number = 0;

  constructor(data: Uint8Array) {
    this.data = data;
  }

  readU8(): number {
    if (this.offset >= this.data.length) {
      throw new Error('Unexpected end of data');
    }
    return this.data[this.offset++]!;
  }

  readU32(): number {
    if (this.offset + 4 > this.data.length) {
      throw new Error('Unexpected end of data');
    }
    const value =
      this.data[this.offset]! |
      (this.data[this.offset + 1]! << 8) |
      (this.data[this.offset + 2]! << 16) |
      (this.data[this.offset + 3]! << 24);
    this.offset += 4;
    return value >>> 0; // Convert to unsigned
  }

  readI32(): number {
    return this.readU32() | 0; // Convert to signed
  }

  readDouble(): number {
    if (this.offset + 8 > this.data.length) {
      throw new Error('Unexpected end of data');
    }
    const view = new DataView(
      this.data.buffer,
      this.data.byteOffset + this.offset,
      8
    );
    this.offset += 8;
    return view.getFloat64(0, true); // Little-endian
  }

  readString(): string {
    const length = this.readU32();
    if (this.offset + length > this.data.length) {
      throw new Error('Unexpected end of data');
    }
    const bytes = this.data.subarray(this.offset, this.offset + length);
    this.offset += length;
    // Decode UTF-8
    const decoder = new TextDecoder('utf-8');
    return decoder.decode(bytes);
  }

  readBytes(length: number): Uint8Array {
    if (this.offset + length > this.data.length) {
      throw new Error('Unexpected end of data');
    }
    const bytes = this.data.slice(this.offset, this.offset + length);
    this.offset += length;
    return bytes;
  }

  hasMore(): boolean {
    return this.offset < this.data.length;
  }
}

export function decodeStructuredClone(data: Uint8Array): unknown {
  const buffer = new ReadBuffer(data);
  const refMap = new Map<number, object>();
  let nextRefId = 0;

  function readValue(): unknown {
    const type = buffer.readU8() as CloneType;

    switch (type) {
      case CloneType.Undefined:
        return undefined;

      case CloneType.Null:
        return null;

      case CloneType.BoolTrue:
        return true;

      case CloneType.BoolFalse:
        return false;

      case CloneType.Int32:
        return buffer.readI32();

      case CloneType.Double:
        return buffer.readDouble();

      case CloneType.String:
        return buffer.readString();

      case CloneType.Object: {
        const obj: Record<string, unknown> = {};
        const refId = nextRefId++;
        refMap.set(refId, obj);

        const propCount = buffer.readU32();
        for (let i = 0; i < propCount; i++) {
          const key = buffer.readString();
          const value = readValue();
          obj[key] = value;
        }
        return obj;
      }

      case CloneType.Array: {
        const length = buffer.readU32();
        const arr: unknown[] = new Array(length);
        const refId = nextRefId++;
        refMap.set(refId, arr);

        for (let i = 0; i < length; i++) {
          arr[i] = readValue();
        }
        return arr;
      }

      case CloneType.Date: {
        const timestamp = buffer.readDouble();
        return new Date(timestamp);
      }

      case CloneType.RegExp: {
        const source = buffer.readString();
        const flags = buffer.readString();
        return new RegExp(source, flags);
      }

      case CloneType.Map: {
        const size = buffer.readU32();
        const map = new Map<unknown, unknown>();
        const refId = nextRefId++;
        refMap.set(refId, map);

        for (let i = 0; i < size; i++) {
          const key = readValue();
          const value = readValue();
          map.set(key, value);
        }
        return map;
      }

      case CloneType.Set: {
        const size = buffer.readU32();
        const set = new Set<unknown>();
        const refId = nextRefId++;
        refMap.set(refId, set);

        for (let i = 0; i < size; i++) {
          const value = readValue();
          set.add(value);
        }
        return set;
      }

      case CloneType.Error:
      case CloneType.EvalError:
      case CloneType.RangeError:
      case CloneType.ReferenceError:
      case CloneType.SyntaxError:
      case CloneType.TypeError:
      case CloneType.URIError: {
        const name = buffer.readString();
        const message = buffer.readString();
        let error: Error;
        switch (type) {
          case CloneType.EvalError:
            error = new EvalError(message);
            break;
          case CloneType.RangeError:
            error = new RangeError(message);
            break;
          case CloneType.ReferenceError:
            error = new ReferenceError(message);
            break;
          case CloneType.SyntaxError:
            error = new SyntaxError(message);
            break;
          case CloneType.TypeError:
            error = new TypeError(message);
            break;
          case CloneType.URIError:
            error = new URIError(message);
            break;
          default:
            error = new Error(message);
            error.name = name;
        }
        return error;
      }

      case CloneType.ArrayBuffer: {
        const byteLength = buffer.readU32();
        const bytes = buffer.readBytes(byteLength);
        return bytes.buffer.slice(
          bytes.byteOffset,
          bytes.byteOffset + bytes.byteLength
        );
      }

      case CloneType.DataView: {
        const bufferByteLength = buffer.readU32();
        const bufferBytes = buffer.readBytes(bufferByteLength);
        const byteOffset = buffer.readU32();
        const byteLength = buffer.readU32();
        const arrayBuffer = bufferBytes.buffer.slice(
          bufferBytes.byteOffset,
          bufferBytes.byteOffset + bufferBytes.byteLength
        );
        return new DataView(arrayBuffer, byteOffset, byteLength);
      }

      case CloneType.Int8Array:
      case CloneType.Uint8Array:
      case CloneType.Uint8ClampedArray:
      case CloneType.Int16Array:
      case CloneType.Uint16Array:
      case CloneType.Int32Array:
      case CloneType.Uint32Array:
      case CloneType.Float32Array:
      case CloneType.Float64Array:
      case CloneType.BigInt64Array:
      case CloneType.BigUint64Array: {
        const bufferByteLength = buffer.readU32();
        const bufferBytes = buffer.readBytes(bufferByteLength);
        const byteOffset = buffer.readU32();
        const length = buffer.readU32();
        const arrayBuffer = bufferBytes.buffer.slice(
          bufferBytes.byteOffset,
          bufferBytes.byteOffset + bufferBytes.byteLength
        );

        switch (type) {
          case CloneType.Int8Array:
            return new Int8Array(arrayBuffer, byteOffset, length);
          case CloneType.Uint8Array:
            return new Uint8Array(arrayBuffer, byteOffset, length);
          case CloneType.Uint8ClampedArray:
            return new Uint8ClampedArray(arrayBuffer, byteOffset, length);
          case CloneType.Int16Array:
            return new Int16Array(arrayBuffer, byteOffset, length);
          case CloneType.Uint16Array:
            return new Uint16Array(arrayBuffer, byteOffset, length);
          case CloneType.Int32Array:
            return new Int32Array(arrayBuffer, byteOffset, length);
          case CloneType.Uint32Array:
            return new Uint32Array(arrayBuffer, byteOffset, length);
          case CloneType.Float32Array:
            return new Float32Array(arrayBuffer, byteOffset, length);
          case CloneType.Float64Array:
            return new Float64Array(arrayBuffer, byteOffset, length);
          case CloneType.BigInt64Array:
            return new BigInt64Array(arrayBuffer, byteOffset, length);
          case CloneType.BigUint64Array:
            return new BigUint64Array(arrayBuffer, byteOffset, length);
        }
      }

      case CloneType.ObjectRef: {
        const refId = buffer.readU32();
        const ref = refMap.get(refId);
        if (ref === undefined) {
          throw new Error(`Invalid object reference: ${refId}`);
        }
        return ref;
      }

      default:
        throw new Error(`Unknown clone type: 0x${type.toString(16)}`);
    }
  }

  return readValue();
}
