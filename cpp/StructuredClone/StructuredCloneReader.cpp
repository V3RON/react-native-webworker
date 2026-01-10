#include "StructuredCloneReader.h"
#include <string>

namespace webworker {

Value StructuredCloneReader::deserialize(Runtime& runtime, const SerializedData& data) {
    ReadBuffer buffer(data.buffer);
    StructuredCloneReader reader(runtime, buffer);
    return reader.readValue();
}

Value StructuredCloneReader::deserialize(Runtime& runtime, const uint8_t* data, size_t size) {
    ReadBuffer buffer(data, size);
    StructuredCloneReader reader(runtime, buffer);
    return reader.readValue();
}

StructuredCloneReader::StructuredCloneReader(Runtime& runtime, ReadBuffer& buffer)
    : runtime_(runtime), buffer_(buffer) {}

Value StructuredCloneReader::readValue() {
    if (!buffer_.hasMore()) {
        throw DataCloneError::invalidData();
    }

    CloneType type = static_cast<CloneType>(buffer_.readU8());

    switch (type) {
        case CloneType::Undefined:
            return Value::undefined();

        case CloneType::Null:
            return Value::null();

        case CloneType::BoolTrue:
            return Value(true);

        case CloneType::BoolFalse:
            return Value(false);

        case CloneType::Int32:
            return Value(static_cast<double>(buffer_.readI32()));

        case CloneType::Double:
            return Value(buffer_.readDouble());

        case CloneType::String:
            return readString();

        case CloneType::Object:
            return readPlainObject();

        case CloneType::Array:
            return readArray();

        case CloneType::Date:
            return readDate();

        case CloneType::RegExp:
            return readRegExp();

        case CloneType::Map:
            return readMap();

        case CloneType::Set:
            return readSet();

        case CloneType::Error:
        case CloneType::EvalError:
        case CloneType::RangeError:
        case CloneType::ReferenceError:
        case CloneType::SyntaxError:
        case CloneType::TypeError:
        case CloneType::URIError:
            return readError(type);

        case CloneType::ArrayBuffer:
            return readArrayBuffer();

        case CloneType::DataView:
            return readDataView();

        case CloneType::Int8Array:
        case CloneType::Uint8Array:
        case CloneType::Uint8ClampedArray:
        case CloneType::Int16Array:
        case CloneType::Uint16Array:
        case CloneType::Int32Array:
        case CloneType::Uint32Array:
        case CloneType::Float32Array:
        case CloneType::Float64Array:
        case CloneType::BigInt64Array:
        case CloneType::BigUint64Array:
            return readTypedArray(type);

        case CloneType::ObjectRef:
            return readObjectRef();

        default:
            throw DataCloneError::invalidData();
    }
}

Value StructuredCloneReader::readString() {
    std::string str = buffer_.readString();
    return Value(runtime_, String::createFromUtf8(runtime_, str));
}

Value StructuredCloneReader::readPlainObject() {
    uint32_t propCount = buffer_.readU32();

    Object obj(runtime_);

    // Register the object before reading properties (for circular refs)
    uint32_t refId = static_cast<uint32_t>(refMap_.size());
    registerObject(refId, obj);

    for (uint32_t i = 0; i < propCount; i++) {
        std::string key = buffer_.readString();
        Value value = readValue();
        obj.setProperty(runtime_, key.c_str(), value);
    }

    return Value(runtime_, obj);
}

Value StructuredCloneReader::readArray() {
    uint32_t length = buffer_.readU32();

    auto global = runtime_.global();
    auto ArrayCtor = global.getPropertyAsFunction(runtime_, "Array");
    Value arrayVal = ArrayCtor.callAsConstructor(runtime_, static_cast<double>(length));
    Object arr = arrayVal.asObject(runtime_);

    // Register the array before reading elements (for circular refs)
    uint32_t refId = static_cast<uint32_t>(refMap_.size());
    registerObject(refId, arr);

    Array arrArray = arr.asArray(runtime_);
    for (uint32_t i = 0; i < length; i++) {
        Value element = readValue();
        arrArray.setValueAtIndex(runtime_, i, element);
    }

    return Value(runtime_, arr);
}

Value StructuredCloneReader::readDate() {
    double timestamp = buffer_.readDouble();

    auto global = runtime_.global();
    auto DateCtor = global.getPropertyAsFunction(runtime_, "Date");
    Value date = DateCtor.callAsConstructor(runtime_, timestamp);

    return date;
}

Value StructuredCloneReader::readRegExp() {
    std::string source = buffer_.readString();
    std::string flags = buffer_.readString();

    auto global = runtime_.global();
    auto RegExpCtor = global.getPropertyAsFunction(runtime_, "RegExp");

    Value regexp = RegExpCtor.callAsConstructor(
        runtime_,
        String::createFromUtf8(runtime_, source),
        String::createFromUtf8(runtime_, flags)
    );

    return regexp;
}

Value StructuredCloneReader::readMap() {
    uint32_t size = buffer_.readU32();

    auto global = runtime_.global();
    auto MapCtor = global.getPropertyAsFunction(runtime_, "Map");
    Value mapVal = MapCtor.callAsConstructor(runtime_);
    Object map = mapVal.asObject(runtime_);

    // Register before reading entries
    uint32_t refId = static_cast<uint32_t>(refMap_.size());
    registerObject(refId, map);

    auto setMethod = map.getPropertyAsFunction(runtime_, "set");

    for (uint32_t i = 0; i < size; i++) {
        Value key = readValue();
        Value value = readValue();
        setMethod.callWithThis(runtime_, map, key, value);
    }

    return Value(runtime_, map);
}

Value StructuredCloneReader::readSet() {
    uint32_t size = buffer_.readU32();

    auto global = runtime_.global();
    auto SetCtor = global.getPropertyAsFunction(runtime_, "Set");
    Value setVal = SetCtor.callAsConstructor(runtime_);
    Object set = setVal.asObject(runtime_);

    // Register before reading values
    uint32_t refId = static_cast<uint32_t>(refMap_.size());
    registerObject(refId, set);

    auto addMethod = set.getPropertyAsFunction(runtime_, "add");

    for (uint32_t i = 0; i < size; i++) {
        Value value = readValue();
        addMethod.callWithThis(runtime_, set, value);
    }

    return Value(runtime_, set);
}

Value StructuredCloneReader::readError(CloneType type) {
    std::string name = buffer_.readString();
    std::string message = buffer_.readString();

    auto global = runtime_.global();

    // Choose the right error constructor
    std::string ctorName;
    switch (type) {
        case CloneType::EvalError:
            ctorName = "EvalError";
            break;
        case CloneType::RangeError:
            ctorName = "RangeError";
            break;
        case CloneType::ReferenceError:
            ctorName = "ReferenceError";
            break;
        case CloneType::SyntaxError:
            ctorName = "SyntaxError";
            break;
        case CloneType::TypeError:
            ctorName = "TypeError";
            break;
        case CloneType::URIError:
            ctorName = "URIError";
            break;
        default:
            ctorName = "Error";
            break;
    }

    auto ErrorCtor = global.getPropertyAsFunction(runtime_, ctorName.c_str());
    Value error = ErrorCtor.callAsConstructor(
        runtime_,
        String::createFromUtf8(runtime_, message)
    );

    return error;
}

Value StructuredCloneReader::readArrayBuffer() {
    uint32_t byteLength = buffer_.readU32();

    auto global = runtime_.global();
    auto ArrayBufferCtor = global.getPropertyAsFunction(runtime_, "ArrayBuffer");
    Value abVal = ArrayBufferCtor.callAsConstructor(runtime_, static_cast<double>(byteLength));
    Object arrayBuffer = abVal.asObject(runtime_);

    if (byteLength > 0) {
        // Create a Uint8Array view to write data
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value viewVal = Uint8ArrayCtor.callAsConstructor(runtime_, abVal);
        Object view = viewVal.asObject(runtime_);

        for (uint32_t i = 0; i < byteLength; i++) {
            uint8_t byte = buffer_.readU8();
            view.setProperty(runtime_, std::to_string(i).c_str(), Value(static_cast<double>(byte)));
        }
    }

    return Value(runtime_, arrayBuffer);
}

Value StructuredCloneReader::readTypedArray(CloneType type) {
    // Read the underlying buffer data
    uint32_t bufferByteLength = buffer_.readU32();

    auto global = runtime_.global();
    auto ArrayBufferCtor = global.getPropertyAsFunction(runtime_, "ArrayBuffer");
    Value abVal = ArrayBufferCtor.callAsConstructor(runtime_, static_cast<double>(bufferByteLength));
    Object arrayBuffer = abVal.asObject(runtime_);

    if (bufferByteLength > 0) {
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value viewVal = Uint8ArrayCtor.callAsConstructor(runtime_, abVal);
        Object view = viewVal.asObject(runtime_);

        for (uint32_t i = 0; i < bufferByteLength; i++) {
            uint8_t byte = buffer_.readU8();
            view.setProperty(runtime_, std::to_string(i).c_str(), Value(static_cast<double>(byte)));
        }
    }

    // Read offset and length
    uint32_t byteOffset = buffer_.readU32();
    uint32_t length = buffer_.readU32();

    // Create the typed array with the correct constructor
    std::string ctorName = getTypedArrayConstructorName(type);
    auto TypedArrayCtor = global.getPropertyAsFunction(runtime_, ctorName.c_str());

    Value typedArray = TypedArrayCtor.callAsConstructor(
        runtime_,
        abVal,
        static_cast<double>(byteOffset),
        static_cast<double>(length)
    );

    return typedArray;
}

Value StructuredCloneReader::readDataView() {
    // Read the underlying buffer data
    uint32_t bufferByteLength = buffer_.readU32();

    auto global = runtime_.global();
    auto ArrayBufferCtor = global.getPropertyAsFunction(runtime_, "ArrayBuffer");
    Value abVal = ArrayBufferCtor.callAsConstructor(runtime_, static_cast<double>(bufferByteLength));
    Object arrayBuffer = abVal.asObject(runtime_);

    if (bufferByteLength > 0) {
        auto Uint8ArrayCtor = global.getPropertyAsFunction(runtime_, "Uint8Array");
        Value viewVal = Uint8ArrayCtor.callAsConstructor(runtime_, abVal);
        Object view = viewVal.asObject(runtime_);

        for (uint32_t i = 0; i < bufferByteLength; i++) {
            uint8_t byte = buffer_.readU8();
            view.setProperty(runtime_, std::to_string(i).c_str(), Value(static_cast<double>(byte)));
        }
    }

    // Read offset and length
    uint32_t byteOffset = buffer_.readU32();
    uint32_t byteLength = buffer_.readU32();

    // Create DataView
    auto DataViewCtor = global.getPropertyAsFunction(runtime_, "DataView");
    Value dataView = DataViewCtor.callAsConstructor(
        runtime_,
        abVal,
        static_cast<double>(byteOffset),
        static_cast<double>(byteLength)
    );

    return dataView;
}

Value StructuredCloneReader::readObjectRef() {
    uint32_t refId = buffer_.readU32();

    auto it = refMap_.find(refId);
    if (it == refMap_.end()) {
        throw DataCloneError::invalidData();
    }

    // Return a copy of the stored Value
    return Value(runtime_, it->second.asObject(runtime_));
}

void StructuredCloneReader::registerObject(uint32_t refId, const Object& obj) {
    refMap_.emplace(refId, Value(runtime_, obj));
}

std::string StructuredCloneReader::getTypedArrayConstructorName(CloneType type) {
    switch (type) {
        case CloneType::Int8Array:
            return "Int8Array";
        case CloneType::Uint8Array:
            return "Uint8Array";
        case CloneType::Uint8ClampedArray:
            return "Uint8ClampedArray";
        case CloneType::Int16Array:
            return "Int16Array";
        case CloneType::Uint16Array:
            return "Uint16Array";
        case CloneType::Int32Array:
            return "Int32Array";
        case CloneType::Uint32Array:
            return "Uint32Array";
        case CloneType::Float32Array:
            return "Float32Array";
        case CloneType::Float64Array:
            return "Float64Array";
        case CloneType::BigInt64Array:
            return "BigInt64Array";
        case CloneType::BigUint64Array:
            return "BigUint64Array";
        default:
            return "Uint8Array";
    }
}

} // namespace webworker
