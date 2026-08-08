#pragma once

#include "comparison_module.h"
#include "comparison_transform.h"
#include "../render/render_document.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trading::app
{
    class ComparisonRenderAdapter final
    {
    public:
        bool Apply(
            const ComparisonModuleSnapshot& snapshot,
            render::RenderDocument& document,
            std::string& error);

        std::uint64_t Revision() const noexcept;
        std::size_t RetainedBytes() const noexcept;
        void ClearCache() noexcept;

    private:
        struct Cache final
        {
            const void* completedIdentity = nullptr;
            const void* primaryIdentity = nullptr;
            std::uint64_t completedRevision = 0;
            double valueDivisor = 1.0;
            ComparisonValueMode valueMode = ComparisonValueMode::RawClose;
            ComparisonTransformResult transformed;
            std::shared_ptr<const std::vector<render::LinePoint>> completedPoints;
        };

        static render::Pane* FindPane(
            render::RenderDocument& document,
            const std::string& paneId) noexcept;

        static bool CopyPrimaryBars(
            const render::RenderDocument& document,
            std::vector<Bar>& bars,
            const void*& identity);

        std::map<std::string, Cache> caches_;
        std::uint64_t revision_ = 0;
    };
}
