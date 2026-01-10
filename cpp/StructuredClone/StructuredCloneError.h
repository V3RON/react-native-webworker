#pragma once

#include <stdexcept>
#include <string>

namespace webworker {

/**
 * DataCloneError - thrown when a value cannot be cloned.
 *
 * This corresponds to the DOMException with name "DataCloneError" in the
 * HTML Structured Clone specification.
 *
 * Types that cause this error:
 * - Symbol
 * - Function
 * - WeakMap
 * - WeakSet
 * - Promise
 * - Proxy
 * - Objects with non-standard internal slots
 */
class DataCloneError : public std::runtime_error {
public:
    explicit DataCloneError(const std::string& message)
        : std::runtime_error("DataCloneError: " + message) {}

    explicit DataCloneError(const char* message)
        : std::runtime_error(std::string("DataCloneError: ") + message) {}

    /**
     * Static factory methods for common error cases
     */
    static DataCloneError cannotCloneSymbol() {
        return DataCloneError("Symbol cannot be cloned");
    }

    static DataCloneError cannotCloneFunction() {
        return DataCloneError("Function cannot be cloned");
    }

    static DataCloneError cannotCloneWeakMap() {
        return DataCloneError("WeakMap cannot be cloned");
    }

    static DataCloneError cannotCloneWeakSet() {
        return DataCloneError("WeakSet cannot be cloned");
    }

    static DataCloneError cannotClonePromise() {
        return DataCloneError("Promise cannot be cloned");
    }

    static DataCloneError cannotCloneProxy() {
        return DataCloneError("Proxy cannot be cloned");
    }

    static DataCloneError cannotCloneType(const std::string& typeName) {
        return DataCloneError("Cannot clone object of type: " + typeName);
    }

    static DataCloneError maxDepthExceeded() {
        return DataCloneError("Maximum recursion depth exceeded");
    }

    static DataCloneError maxSizeExceeded() {
        return DataCloneError("Maximum serialization size exceeded");
    }

    static DataCloneError detachedArrayBuffer() {
        return DataCloneError("Cannot clone detached ArrayBuffer");
    }

    static DataCloneError invalidData() {
        return DataCloneError("Invalid serialized data");
    }
};

} // namespace webworker
