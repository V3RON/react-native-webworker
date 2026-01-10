#pragma once

#include <jsi/jsi.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>

namespace webworker {

using namespace facebook::jsi;

class ResponseHostObject : public HostObject {
public:
    ResponseHostObject(int status, 
                       const std::unordered_map<std::string, std::string>& headers,
                       std::vector<uint8_t> data)
        : status_(status), headers_(headers), data_(std::move(data)) {}

    Value get(Runtime& rt, const PropNameID& name) override {
        std::string prop = name.utf8(rt);

        if (prop == "status") {
            return status_;
        }

        if (prop == "headers") {
            Object headersObj(rt);
            for (const auto& pair : headers_) {
                headersObj.setProperty(rt, pair.first.c_str(), pair.second.c_str());
            }
            return headersObj;
        }

        if (prop == "text") {
            return Function::createFromHostFunction(rt, name, 0,
                [this](Runtime& rt, const Value&, const Value*, size_t) {
                    std::string textStr(data_.begin(), data_.end());
                    return String::createFromUtf8(rt, textStr);
                });
        }

        if (prop == "arrayBuffer") {
            return Function::createFromHostFunction(rt, name, 0,
                [this](Runtime& rt, const Value&, const Value*, size_t) {
                    auto arrayBuffer = rt.global().getPropertyAsFunction(rt, "ArrayBuffer")
                                               .callAsConstructor(rt, (int)data_.size())
                                               .getObject(rt).getArrayBuffer(rt);
                    
                    if (data_.size() > 0) {
                        memcpy(arrayBuffer.data(rt), data_.data(), data_.size());
                    }
                    
                    return arrayBuffer;
                });
        }

        return Value::undefined();
    }

private:
    int status_;
    std::unordered_map<std::string, std::string> headers_;
    std::vector<uint8_t> data_;
};

} // namespace webworker
