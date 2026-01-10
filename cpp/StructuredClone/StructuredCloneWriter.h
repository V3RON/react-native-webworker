#pragma once

#include <jsi/jsi.h>
#include <unordered_map>
#include <cstdint>

#include "StructuredCloneTypes.h"
#include "StructuredCloneError.h"

namespace webworker {

using namespace facebook::jsi;

/**
 * StructuredCloneWriter - Serializes JavaScript values using the Structured Clone algorithm.
 *
 * This class implements the serialization portion of the HTML Structured Clone
 * specification. It converts JSI Values into a binary format that can be
 * transferred between workers and deserialized back to JavaScript values.
 *
 * Supported types:
 * - Primitives: undefined, null, boolean, number, string
 * - Objects: plain objects, arrays
 * - Special objects: Date, RegExp, Map, Set, Error types
 * - Binary data: ArrayBuffer, TypedArrays, DataView
 *
 * Features:
 * - Circular reference detection via memory map
 * - Throws DataCloneError for non-cloneable types (Function, Symbol, etc.)
 */
class StructuredCloneWriter {
public:
    /**
     * Serialize a JavaScript value to binary format.
     *
     * @param runtime The JSI runtime
     * @param value The value to serialize
     * @return SerializedData containing the binary representation
     * @throws DataCloneError if the value cannot be cloned
     */
    static SerializedData serialize(Runtime& runtime, const Value& value);

private:
    StructuredCloneWriter(Runtime& runtime);

    void writeValue(const Value& value);

    // Primitive writers
    void writeUndefined();
    void writeNull();
    void writeBoolean(bool value);
    void writeNumber(double value);
    void writeString(const String& str);

    // Object writers
    void writePlainObject(const Object& obj);
    void writeArray(const Object& arr);
    void writeDate(const Object& date);
    void writeRegExp(const Object& regexp);
    void writeMap(const Object& map);
    void writeSet(const Object& set);
    void writeError(const Object& error);

    // Binary data writers
    void writeArrayBuffer(const Object& arrayBuffer);
    void writeTypedArray(const Object& typedArray, CloneType type);
    void writeDataView(const Object& dataView);

    // Reference handling
    bool tryWriteReference(const Object& obj);
    void registerObject(const Object& obj);

    // Type detection helpers
    std::string getObjectType(const Object& obj);
    bool isPlainObject(const Object& obj);
    CloneType getTypedArrayType(const std::string& typeName);

    // Validation
    void assertCloneable(const Value& value);
    void checkDepth();
    void checkSize();

    Runtime& runtime_;
    WriteBuffer buffer_;
    std::unordered_map<uintptr_t, uint32_t> memoryMap_;
    uint32_t nextRefId_ = 0;
    size_t depth_ = 0;
};

} // namespace webworker
