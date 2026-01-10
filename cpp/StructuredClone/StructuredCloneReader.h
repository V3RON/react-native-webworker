#pragma once

#include <jsi/jsi.h>
#include <unordered_map>
#include <cstdint>

#include "StructuredCloneTypes.h"
#include "StructuredCloneError.h"

namespace webworker {

using namespace facebook::jsi;

/**
 * StructuredCloneReader - Deserializes binary data to JavaScript values.
 *
 * This class implements the deserialization portion of the HTML Structured Clone
 * specification. It converts binary data (produced by StructuredCloneWriter)
 * back into JSI Values.
 *
 * Features:
 * - Circular reference resolution via reference map
 * - Reconstructs all supported types including Date, RegExp, Map, Set, etc.
 */
class StructuredCloneReader {
public:
    /**
     * Deserialize binary data to a JavaScript value.
     *
     * @param runtime The JSI runtime
     * @param data The serialized data to deserialize
     * @return The deserialized JavaScript value
     * @throws DataCloneError if the data is malformed
     */
    static Value deserialize(Runtime& runtime, const SerializedData& data);

    /**
     * Deserialize from raw buffer.
     */
    static Value deserialize(Runtime& runtime, const uint8_t* data, size_t size);

private:
    StructuredCloneReader(Runtime& runtime, ReadBuffer& buffer);

    Value readValue();

    // Primitive readers
    Value readString();

    // Object readers
    Value readPlainObject();
    Value readArray();
    Value readDate();
    Value readRegExp();
    Value readMap();
    Value readSet();
    Value readError(CloneType type);

    // Binary data readers
    Value readArrayBuffer();
    Value readTypedArray(CloneType type);
    Value readDataView();

    // Reference handling
    Value readObjectRef();
    void registerObject(uint32_t refId, const Object& obj);

    // Helper to get typed array constructor name
    std::string getTypedArrayConstructorName(CloneType type);

    Runtime& runtime_;
    ReadBuffer& buffer_;
    std::unordered_map<uint32_t, Object> refMap_;
};

} // namespace webworker
