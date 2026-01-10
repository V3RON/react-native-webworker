#include "StructuredCloneWriter.h"
#include <cmath>
#include <limits>
#include <string>

namespace webworker {

SerializedData StructuredCloneWriter::serialize(Runtime& runtime, const Value& value) {
    StructuredCloneWriter writer(runtime);
    writer.writeValue(value);

    SerializedData data;
    data.buffer = writer.buffer_.take();
    return data;
}

StructuredCloneWriter::StructuredCloneWriter(Runtime& runtime)
    : runtime_(runtime) {}

void StructuredCloneWriter::writeValue(const Value& value) {
    checkDepth();
    checkSize();

    if (value.isUndefined()) {
        writeUndefined();
    } else if (value.isNull()) {
        writeNull();
    } else if (value.isBool()) {
        writeBoolean(value.getBool());
    } else if (value.isNumber()) {
        writeNumber(value.asNumber());
    } else if (value.isString()) {
        writeString(value.asString(runtime_));
    } else if (value.isSymbol()) {
        throw DataCloneError::cannotCloneSymbol();
    } else if (value.isObject()) {
        Object obj = value.asObject(runtime_);

        // Check if it's a function first
        if (obj.isFunction(runtime_)) {
            throw DataCloneError::cannotCloneFunction();
        }

        // Check for circular reference
        if (tryWriteReference(obj)) {
            return;
        }

        // Register object for future reference detection
        registerObject(obj);

        // Determine object type and serialize appropriately
        std::string typeName = getObjectType(obj);

        if (typeName == "[object Array]") {
            writeArray(obj);
        } else if (typeName == "[object Date]") {
            writeDate(obj);
        } else if (typeName == "[object RegExp]") {
            writeRegExp(obj);
        } else if (typeName == "[object Map]") {
            writeMap(obj);
        } else if (typeName == "[object Set]") {
            writeSet(obj);
        } else if (typeName == "[object ArrayBuffer]") {
            writeArrayBuffer(obj);
        } else if (typeName == "[object DataView]") {
            writeDataView(obj);
        } else if (typeName == "[object Int8Array]") {
            writeTypedArray(obj, CloneType::Int8Array);
        } else if (typeName == "[object Uint8Array]") {
            writeTypedArray(obj, CloneType::Uint8Array);
        } else if (typeName == "[object Uint8ClampedArray]") {
            writeTypedArray(obj, CloneType::Uint8ClampedArray);
        } else if (typeName == "[object Int16Array]") {
            writeTypedArray(obj, CloneType::Int16Array);
        } else if (typeName == "[object Uint16Array]") {
            writeTypedArray(obj, CloneType::Uint16Array);
        } else if (typeName == "[object Int32Array]") {
            writeTypedArray(obj, CloneType::Int32Array);
        } else if (typeName == "[object Uint32Array]") {
            writeTypedArray(obj, CloneType::Uint32Array);
        } else if (typeName == "[object Float32Array]") {
            writeTypedArray(obj, CloneType::Float32Array);
        } else if (typeName == "[object Float64Array]") {
            writeTypedArray(obj, CloneType::Float64Array);
        } else if (typeName == "[object BigInt64Array]") {
            writeTypedArray(obj, CloneType::BigInt64Array);
        } else if (typeName == "[object BigUint64Array]") {
            writeTypedArray(obj, CloneType::BigUint64Array);
        } else if (typeName == "[object Error]") {
            writeError(obj);
        } else if (typeName == "[object EvalError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::EvalError));
            // Write name and message
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("EvalError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object RangeError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::RangeError));
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("RangeError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object ReferenceError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::ReferenceError));
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("ReferenceError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object SyntaxError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::SyntaxError));
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("SyntaxError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object TypeError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::TypeError));
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("TypeError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object URIError]") {
            buffer_.writeU8(static_cast<uint8_t>(CloneType::URIError));
            auto message = obj.getProperty(runtime_, "message");
            std::string msgStr = message.isString() ? message.asString(runtime_).utf8(runtime_) : "";
            buffer_.writeString("URIError");
            buffer_.writeString(msgStr);
        } else if (typeName == "[object WeakMap]") {
            throw DataCloneError::cannotCloneWeakMap();
        } else if (typeName == "[object WeakSet]") {
            throw DataCloneError::cannotCloneWeakSet();
        } else if (typeName == "[object Promise]") {
            throw DataCloneError::cannotClonePromise();
        } else if (typeName == "[object Object]" || isPlainObject(obj)) {
            writePlainObject(obj);
        } else {
            // Unknown object type - try to serialize as plain object
            // This handles custom classes that have enumerable properties
            writePlainObject(obj);
        }
    } else {
        throw DataCloneError("Unknown value type");
    }
}

void StructuredCloneWriter::writeUndefined() {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Undefined));
}

void StructuredCloneWriter::writeNull() {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Null));
}

void StructuredCloneWriter::writeBoolean(bool value) {
    buffer_.writeU8(static_cast<uint8_t>(value ? CloneType::BoolTrue : CloneType::BoolFalse));
}

void StructuredCloneWriter::writeNumber(double value) {
    // Check if the number can be represented as int32
    if (std::isfinite(value) &&
        value >= std::numeric_limits<int32_t>::min() &&
        value <= std::numeric_limits<int32_t>::max() &&
        value == static_cast<double>(static_cast<int32_t>(value))) {
        buffer_.writeU8(static_cast<uint8_t>(CloneType::Int32));
        buffer_.writeI32(static_cast<int32_t>(value));
    } else {
        buffer_.writeU8(static_cast<uint8_t>(CloneType::Double));
        buffer_.writeDouble(value);
    }
}

void StructuredCloneWriter::writeString(const String& str) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::String));
    std::string utf8 = str.utf8(runtime_);
    buffer_.writeString(utf8);
}

void StructuredCloneWriter::writePlainObject(const Object& obj) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Object));

    // Get own enumerable property names
    Array propNames = obj.getPropertyNames(runtime_);
    size_t length = propNames.size(runtime_);

    buffer_.writeU32(static_cast<uint32_t>(length));

    depth_++;
    for (size_t i = 0; i < length; i++) {
        Value keyValue = propNames.getValueAtIndex(runtime_, i);
        std::string key = keyValue.asString(runtime_).utf8(runtime_);

        buffer_.writeString(key);

        Value propValue = obj.getProperty(runtime_, key.c_str());
        writeValue(propValue);
    }
    depth_--;
}

void StructuredCloneWriter::writeArray(const Object& arr) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Array));

    Value lengthVal = arr.getProperty(runtime_, "length");
    uint32_t length = static_cast<uint32_t>(lengthVal.asNumber());

    buffer_.writeU32(length);

    depth_++;
    Array arrObj = arr.asArray(runtime_);
    for (uint32_t i = 0; i < length; i++) {
        Value elem = arrObj.getValueAtIndex(runtime_, i);
        writeValue(elem);
    }
    depth_--;
}

void StructuredCloneWriter::writeDate(const Object& date) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Date));

    // Call getTime() to get the timestamp
    auto getTime = date.getPropertyAsFunction(runtime_, "getTime");
    Value timeValue = getTime.callWithThis(runtime_, date);
    double timestamp = timeValue.asNumber();

    buffer_.writeDouble(timestamp);
}

void StructuredCloneWriter::writeRegExp(const Object& regexp) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::RegExp));

    // Get source and flags
    Value sourceVal = regexp.getProperty(runtime_, "source");
    Value flagsVal = regexp.getProperty(runtime_, "flags");

    std::string source = sourceVal.isString() ? sourceVal.asString(runtime_).utf8(runtime_) : "";
    std::string flags = flagsVal.isString() ? flagsVal.asString(runtime_).utf8(runtime_) : "";

    buffer_.writeString(source);
    buffer_.writeString(flags);
}

void StructuredCloneWriter::writeMap(const Object& map) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Map));

    // Get size
    Value sizeVal = map.getProperty(runtime_, "size");
    uint32_t size = static_cast<uint32_t>(sizeVal.asNumber());

    buffer_.writeU32(size);

    // Use forEach to iterate
    // We need to collect entries first, then serialize them
    // Create a temporary array to store entries
    auto entriesMethod = map.getPropertyAsFunction(runtime_, "entries");
    Value entriesIterator = entriesMethod.callWithThis(runtime_, map);
    Object iterator = entriesIterator.asObject(runtime_);

    depth_++;
    for (uint32_t i = 0; i < size; i++) {
        auto nextMethod = iterator.getPropertyAsFunction(runtime_, "next");
        Value result = nextMethod.callWithThis(runtime_, iterator);
        Object resultObj = result.asObject(runtime_);

        Value done = resultObj.getProperty(runtime_, "done");
        if (done.isBool() && done.getBool()) {
            break;
        }

        Value entryValue = resultObj.getProperty(runtime_, "value");
        Array entry = entryValue.asObject(runtime_).asArray(runtime_);

        Value key = entry.getValueAtIndex(runtime_, 0);
        Value val = entry.getValueAtIndex(runtime_, 1);

        writeValue(key);
        writeValue(val);
    }
    depth_--;
}

void StructuredCloneWriter::writeSet(const Object& set) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Set));

    // Get size
    Value sizeVal = set.getProperty(runtime_, "size");
    uint32_t size = static_cast<uint32_t>(sizeVal.asNumber());

    buffer_.writeU32(size);

    // Use iterator to get values
    auto valuesMethod = set.getPropertyAsFunction(runtime_, "values");
    Value valuesIterator = valuesMethod.callWithThis(runtime_, set);
    Object iterator = valuesIterator.asObject(runtime_);

    depth_++;
    for (uint32_t i = 0; i < size; i++) {
        auto nextMethod = iterator.getPropertyAsFunction(runtime_, "next");
        Value result = nextMethod.callWithThis(runtime_, iterator);
        Object resultObj = result.asObject(runtime_);

        Value done = resultObj.getProperty(runtime_, "done");
        if (done.isBool() && done.getBool()) {
            break;
        }

        Value val = resultObj.getProperty(runtime_, "value");
        writeValue(val);
    }
    depth_--;
}

void StructuredCloneWriter::writeError(const Object& error) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::Error));

    // Get name and message
    Value nameVal = error.getProperty(runtime_, "name");
    Value messageVal = error.getProperty(runtime_, "message");

    std::string name = nameVal.isString() ? nameVal.asString(runtime_).utf8(runtime_) : "Error";
    std::string message = messageVal.isString() ? messageVal.asString(runtime_).utf8(runtime_) : "";

    buffer_.writeString(name);
    buffer_.writeString(message);
}

void StructuredCloneWriter::writeArrayBuffer(const Object& arrayBuffer) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::ArrayBuffer));

    // Get byteLength
    Value byteLengthVal = arrayBuffer.getProperty(runtime_, "byteLength");
    uint32_t byteLength = static_cast<uint32_t>(byteLengthVal.asNumber());

    buffer_.writeU32(byteLength);

    if (byteLength > 0) {
        // Get the data using Uint8Array view
        auto global = runtime_.global();
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value view = Uint8ArrayCtor.callAsConstructor(runtime_, arrayBuffer);
        Object viewObj = view.asObject(runtime_);

        // Read each byte
        for (uint32_t i = 0; i < byteLength; i++) {
            Value byteVal = viewObj.getProperty(runtime_, std::to_string(i).c_str());
            buffer_.writeU8(static_cast<uint8_t>(byteVal.asNumber()));
        }
    }
}

void StructuredCloneWriter::writeTypedArray(const Object& typedArray, CloneType type) {
    buffer_.writeU8(static_cast<uint8_t>(type));

    // Get buffer, byteOffset, and length
    Value bufferVal = typedArray.getProperty(runtime_, "buffer");
    Value byteOffsetVal = typedArray.getProperty(runtime_, "byteOffset");
    Value lengthVal = typedArray.getProperty(runtime_, "length");

    Object buffer = bufferVal.asObject(runtime_);
    uint32_t byteOffset = static_cast<uint32_t>(byteOffsetVal.asNumber());
    uint32_t length = static_cast<uint32_t>(lengthVal.asNumber());

    // Get byte length of underlying buffer
    Value bufferByteLengthVal = buffer.getProperty(runtime_, "byteLength");
    uint32_t bufferByteLength = static_cast<uint32_t>(bufferByteLengthVal.asNumber());

    // Write the underlying ArrayBuffer data
    buffer_.writeU32(bufferByteLength);

    if (bufferByteLength > 0) {
        auto global = runtime_.global();
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value view = Uint8ArrayCtor.callAsConstructor(runtime_, buffer);
        Object viewObj = view.asObject(runtime_);

        for (uint32_t i = 0; i < bufferByteLength; i++) {
            Value byteVal = viewObj.getProperty(runtime_, std::to_string(i).c_str());
            buffer_.writeU8(static_cast<uint8_t>(byteVal.asNumber()));
        }
    }

    // Write offset and length
    buffer_.writeU32(byteOffset);
    buffer_.writeU32(length);
}

void StructuredCloneWriter::writeDataView(const Object& dataView) {
    buffer_.writeU8(static_cast<uint8_t>(CloneType::DataView));

    // Get buffer, byteOffset, and byteLength
    Value bufferVal = dataView.getProperty(runtime_, "buffer");
    Value byteOffsetVal = dataView.getProperty(runtime_, "byteOffset");
    Value byteLengthVal = dataView.getProperty(runtime_, "byteLength");

    Object buffer = bufferVal.asObject(runtime_);
    uint32_t byteOffset = static_cast<uint32_t>(byteOffsetVal.asNumber());
    uint32_t byteLength = static_cast<uint32_t>(byteLengthVal.asNumber());

    // Get byte length of underlying buffer
    Value bufferByteLengthVal = buffer.getProperty(runtime_, "byteLength");
    uint32_t bufferByteLength = static_cast<uint32_t>(bufferByteLengthVal.asNumber());

    // Write the underlying ArrayBuffer data
    buffer_.writeU32(bufferByteLength);

    if (bufferByteLength > 0) {
        auto global = runtime_.global();
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value view = Uint8ArrayCtor.callAsConstructor(runtime_, buffer);
        Object viewObj = view.asObject(runtime_);

        for (uint32_t i = 0; i < bufferByteLength; i++) {
            Value byteVal = viewObj.getProperty(runtime_, std::to_string(i).c_str());
            buffer_.writeU8(static_cast<uint8_t>(byteVal.asNumber()));
        }
    }

    // Write offset and length
    buffer_.writeU32(byteOffset);
    buffer_.writeU32(byteLength);
}

bool StructuredCloneWriter::tryWriteReference(const Object& obj) {
    // Get a unique identifier for the object (pointer to internal representation)
    // This is a bit hacky but works for detecting object identity
    uintptr_t objId = reinterpret_cast<uintptr_t>(&obj);

    // In JSI, we need to use a different approach since Object is a wrapper
    // We'll use the toString representation as a fallback identity check
    // For now, we track by pointer which works within a single serialization

    auto it = memoryMap_.find(objId);
    if (it != memoryMap_.end()) {
        buffer_.writeU8(static_cast<uint8_t>(CloneType::ObjectRef));
        buffer_.writeU32(it->second);
        return true;
    }

    return false;
}

void StructuredCloneWriter::registerObject(const Object& obj) {
    uintptr_t objId = reinterpret_cast<uintptr_t>(&obj);
    memoryMap_[objId] = nextRefId_++;
}

std::string StructuredCloneWriter::getObjectType(const Object& obj) {
    // Use Object.prototype.toString.call(obj) to get accurate type
    auto global = runtime_.global();
    auto Object_proto = global.getPropertyAsObject(runtime_, "Object")
                            .getPropertyAsObject(runtime_, "prototype");
    auto toString = Object_proto.getPropertyAsFunction(runtime_, "toString");

    Value result = toString.callWithThis(runtime_, obj);
    return result.asString(runtime_).utf8(runtime_);
}

bool StructuredCloneWriter::isPlainObject(const Object& obj) {
    std::string type = getObjectType(obj);
    return type == "[object Object]";
}

void StructuredCloneWriter::checkDepth() {
    if (depth_ >= CloneConstants::MAX_DEPTH) {
        throw DataCloneError::maxDepthExceeded();
    }
}

void StructuredCloneWriter::checkSize() {
    if (buffer_.size() >= CloneConstants::MAX_SIZE) {
        throw DataCloneError::maxSizeExceeded();
    }
}

} // namespace webworker
