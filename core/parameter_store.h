#pragma once

#include <array>
#include <string>
#include <vector>

namespace trading
{
    enum class ParameterType
    {
        Integer,
        Float,
        Boolean,
        Color4
    };

    struct ParameterDescriptor final
    {
        std::string group;
        std::string key;
        std::string displayName;
        ParameterType type = ParameterType::Integer;
        void* storage = nullptr;
        double minimum = 0.0;
        double maximum = 0.0;
        std::string description;
    };

    class ParameterStore final
    {
    public:
        bool RegisterInteger(
            const std::string& group,
            const std::string& key,
            const std::string& displayName,
            int& value,
            int minimum,
            int maximum,
            const std::string& description = {});

        bool RegisterFloat(
            const std::string& group,
            const std::string& key,
            const std::string& displayName,
            float& value,
            float minimum,
            float maximum,
            const std::string& description = {});

        bool RegisterBoolean(
            const std::string& group,
            const std::string& key,
            const std::string& displayName,
            bool& value,
            const std::string& description = {});

        bool RegisterColor4(
            const std::string& group,
            const std::string& key,
            const std::string& displayName,
            float (&value)[4],
            const std::string& description = {});

        const std::vector<ParameterDescriptor>& Descriptors() const noexcept;

        std::string SaveJson() const;

        bool LoadJson(
            const std::string& json,
            std::string& error,
            bool rejectUnknownKeys = true);

        bool SaveFile(
            const std::string& path,
            std::string& error) const;

        bool LoadFile(
            const std::string& path,
            std::string& error,
            bool rejectUnknownKeys = true);

    private:
        bool Register(
            ParameterDescriptor descriptor);

        const ParameterDescriptor* Find(
            const std::string& key) const noexcept;

        std::vector<ParameterDescriptor> descriptors_;
    };
}
