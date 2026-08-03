#include "indicator_engine.h"

#include "json_lite.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace trading::indicators
{
    namespace
    {
        bool HasOnlyRootKeys(const json_lite::Value::Object& object)
        {
            for (const auto& entry : object) {
                if (
                    entry.first != "id" &&
                    entry.first != "type" &&
                    entry.first != "parameters")
                {
                    return false;
                }
            }
            return true;
        }

        std::string FormatNumber(double value)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::setprecision(std::numeric_limits<double>::max_digits10)
                << value;
            return out.str();
        }
    }

    bool ValidateIndicatorSpec(
        const IndicatorSpec& spec,
        std::string& error)
    {
        error.clear();

        if (spec.id.empty()) {
            error = "indicator id is required";
            return false;
        }
        if (spec.type.empty()) {
            error = "indicator type is required";
            return false;
        }

        for (const auto& parameter : spec.parameters) {
            if (parameter.first.empty()) {
                error = "indicator parameter key is empty";
                return false;
            }
            if (!std::isfinite(parameter.second)) {
                error = "indicator parameter is not finite: " + parameter.first;
                return false;
            }
        }

        return true;
    }

    bool SerializeIndicatorSpec(
        const IndicatorSpec& spec,
        std::string& json,
        std::string& error)
    {
        json.clear();
        if (!ValidateIndicatorSpec(spec, error)) return false;

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\"id\":" << json_lite::EscapeString(spec.id)
            << ",\"type\":" << json_lite::EscapeString(spec.type)
            << ",\"parameters\":{";

        bool first = true;
        for (const auto& parameter : spec.parameters) {
            if (!first) out << ',';
            first = false;
            out << json_lite::EscapeString(parameter.first)
                << ':' << FormatNumber(parameter.second);
        }

        out << "}}";
        json = out.str();
        return true;
    }

    bool ParseIndicatorSpec(
        const std::string& json,
        IndicatorSpec& spec,
        std::string& error)
    {
        error.clear();
        const json_lite::ParseResult parsed = json_lite::Parse(json);
        if (!parsed.ok) {
            error = "indicator JSON parse failed: " + parsed.error;
            return false;
        }
        if (!parsed.value.IsObject()) {
            error = "indicator document must be an object";
            return false;
        }

        const json_lite::Value::Object& root = parsed.value.AsObject();
        if (!HasOnlyRootKeys(root)) {
            error = "indicator document contains an unknown root key";
            return false;
        }

        const json_lite::Value* id = parsed.value.Find("id");
        const json_lite::Value* type = parsed.value.Find("type");
        const json_lite::Value* parameters = parsed.value.Find("parameters");
        if (id == nullptr || !id->IsString()) {
            error = "indicator id must be a string";
            return false;
        }
        if (type == nullptr || !type->IsString()) {
            error = "indicator type must be a string";
            return false;
        }
        if (parameters == nullptr || !parameters->IsObject()) {
            error = "indicator parameters must be an object";
            return false;
        }

        IndicatorSpec candidate;
        candidate.id = id->AsString();
        candidate.type = type->AsString();

        for (const auto& parameter : parameters->AsObject()) {
            if (!parameter.second.IsNumber()) {
                error = "indicator parameter must be numeric: " + parameter.first;
                return false;
            }
            candidate.parameters.emplace(
                parameter.first,
                parameter.second.AsNumber());
        }

        if (!ValidateIndicatorSpec(candidate, error)) return false;
        spec = std::move(candidate);
        return true;
    }

    bool TryGetIntegerParameter(
        const IndicatorSpec& spec,
        const std::string& key,
        int minimum,
        int maximum,
        int& value,
        std::string& error)
    {
        error.clear();
        const auto found = spec.parameters.find(key);
        if (found == spec.parameters.end()) {
            error = "missing indicator parameter: " + key;
            return false;
        }

        const double number = found->second;
        if (
            !std::isfinite(number) ||
            std::floor(number) != number ||
            number < static_cast<double>(minimum) ||
            number > static_cast<double>(maximum))
        {
            error = "invalid integer indicator parameter: " + key;
            return false;
        }

        value = static_cast<int>(number);
        return true;
    }

    const char* IndicatorFaultMessage(IndicatorFault fault) noexcept
    {
        switch (fault) {
        case IndicatorFault::None:
            return "none";
        case IndicatorFault::InvalidInstance:
            return "invalid indicator instance";
        case IndicatorFault::InvalidInput:
            return "invalid indicator input";
        case IndicatorFault::TimestampMovedBackward:
            return "indicator timestamp moved backward";
        }
        return "unknown indicator fault";
    }

    IndicatorInstance::IndicatorInstance(
        std::string type,
        void* state,
        ResetFunction reset,
        UpdateFunction update,
        DestroyFunction destroy,
        RetainedBytesFunction retainedBytes) noexcept
        : type_(std::move(type)),
          state_(state),
          reset_(reset),
          update_(update),
          destroy_(destroy),
          retainedBytes_(retainedBytes)
    {
    }

    IndicatorInstance::~IndicatorInstance()
    {
        Release();
    }

    IndicatorInstance::IndicatorInstance(IndicatorInstance&& other) noexcept
        : type_(std::move(other.type_)),
          state_(other.state_),
          reset_(other.reset_),
          update_(other.update_),
          destroy_(other.destroy_),
          retainedBytes_(other.retainedBytes_)
    {
        other.state_ = nullptr;
        other.reset_ = nullptr;
        other.update_ = nullptr;
        other.destroy_ = nullptr;
        other.retainedBytes_ = nullptr;
    }

    IndicatorInstance& IndicatorInstance::operator=(
        IndicatorInstance&& other) noexcept
    {
        if (this == &other) return *this;

        Release();
        type_ = std::move(other.type_);
        state_ = other.state_;
        reset_ = other.reset_;
        update_ = other.update_;
        destroy_ = other.destroy_;
        retainedBytes_ = other.retainedBytes_;

        other.state_ = nullptr;
        other.reset_ = nullptr;
        other.update_ = nullptr;
        other.destroy_ = nullptr;
        other.retainedBytes_ = nullptr;
        return *this;
    }

    bool IndicatorInstance::IsValid() const noexcept
    {
        return
            state_ != nullptr &&
            reset_ != nullptr &&
            update_ != nullptr &&
            destroy_ != nullptr;
    }

    const std::string& IndicatorInstance::Type() const noexcept
    {
        return type_;
    }

    void IndicatorInstance::Reset() noexcept
    {
        if (IsValid()) reset_(state_);
    }

    IndicatorValue IndicatorInstance::Update(const Bar& bar) noexcept
    {
        if (!IsValid()) {
            IndicatorValue result;
            result.timestampMs = bar.closeTimestampMs;
            result.fault = IndicatorFault::InvalidInstance;
            return result;
        }
        return update_(state_, bar);
    }

    std::size_t IndicatorInstance::RetainedBytes() const noexcept
    {
        if (!IsValid() || retainedBytes_ == nullptr) return 0;
        return retainedBytes_(state_);
    }

    void IndicatorInstance::Release() noexcept
    {
        if (state_ != nullptr && destroy_ != nullptr) {
            destroy_(state_);
        }

        type_.clear();
        state_ = nullptr;
        reset_ = nullptr;
        update_ = nullptr;
        destroy_ = nullptr;
        retainedBytes_ = nullptr;
    }

    bool IndicatorRegistry::Register(
        const std::string& type,
        IndicatorFactory factory)
    {
        if (type.empty() || !factory) return false;
        return factories_.emplace(type, std::move(factory)).second;
    }

    bool IndicatorRegistry::Contains(const std::string& type) const noexcept
    {
        return factories_.find(type) != factories_.end();
    }

    IndicatorInstance IndicatorRegistry::Create(
        const IndicatorSpec& spec,
        std::string& error) const
    {
        error.clear();
        if (!ValidateIndicatorSpec(spec, error)) return {};

        const auto found = factories_.find(spec.type);
        if (found == factories_.end()) {
            error = "unknown indicator type: " + spec.type;
            return {};
        }

        IndicatorInstance instance = found->second(spec, error);
        if (!instance.IsValid() && error.empty()) {
            error = "indicator factory returned an invalid instance: " + spec.type;
        }
        return instance;
    }

    std::vector<std::string> IndicatorRegistry::Types() const
    {
        std::vector<std::string> types;
        types.reserve(factories_.size());
        for (const auto& entry : factories_) {
            types.push_back(entry.first);
        }
        return types;
    }

    bool CalculateBatch(
        IndicatorInstance& instance,
        const std::vector<Bar>& bars,
        std::vector<IndicatorValue>& output,
        std::string& error)
    {
        error.clear();
        output.clear();

        if (!instance.IsValid()) {
            error = IndicatorFaultMessage(IndicatorFault::InvalidInstance);
            return false;
        }

        instance.Reset();
        output.reserve(bars.size());

        for (const Bar& bar : bars) {
            const IndicatorValue value = instance.Update(bar);
            if (value.fault != IndicatorFault::None) {
                error = IndicatorFaultMessage(value.fault);
                output.clear();
                return false;
            }
            output.push_back(value);
        }

        return true;
    }
}
