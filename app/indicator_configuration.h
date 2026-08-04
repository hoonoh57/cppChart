#pragma once

#include "indicator_render_adapter.h"
#include "../core/indicator_engine.h"

#include <string>
#include <vector>

namespace trading::app
{
    struct IndicatorCatalogEntry final
    {
        std::string type;
        std::string displayName;
    };

    struct IndicatorInstanceDefinition final
    {
        indicators::IndicatorSpec spec;
        bool visible = true;
        std::vector<IndicatorOutputBinding> outputs;
        std::vector<IndicatorReferenceBinding> references;
    };

    const std::vector<IndicatorCatalogEntry>&
    IndicatorCatalog() noexcept;

    bool CreateIndicatorDefinition(
        const indicators::IndicatorSpec& spec,
        IndicatorInstanceDefinition& definition,
        std::string& error);

    bool CreateDefaultIndicatorDefinition(
        const std::string& type,
        const std::string& id,
        IndicatorInstanceDefinition& definition,
        std::string& error);

    std::vector<IndicatorInstanceDefinition>
    InitialIndicatorDefinitions();

    std::vector<indicators::IndicatorSpec>
    VisibleIndicatorSpecs(
        const std::vector<IndicatorInstanceDefinition>& definitions);

    bool BuildIndicatorRenderPlan(
        const std::vector<IndicatorInstanceDefinition>& definitions,
        IndicatorRenderPlan& plan,
        std::string& error);

    const IndicatorInstanceDefinition* FindIndicatorDefinition(
        const std::vector<IndicatorInstanceDefinition>& definitions,
        const std::string& id) noexcept;

    IndicatorInstanceDefinition* FindIndicatorDefinition(
        std::vector<IndicatorInstanceDefinition>& definitions,
        const std::string& id) noexcept;

    std::string NextIndicatorInstanceId(
        const std::string& type,
        const std::vector<IndicatorInstanceDefinition>& definitions);

    bool DuplicateIndicatorDefinition(
        const IndicatorInstanceDefinition& source,
        const std::string& newId,
        std::size_t colorVariant,
        IndicatorInstanceDefinition& duplicate,
        std::string& error);

    void ApplyIndicatorColorVariant(
        IndicatorInstanceDefinition& definition,
        std::size_t colorVariant) noexcept;

    bool MoveIndicatorToPane(
        IndicatorInstanceDefinition& definition,
        const std::string& paneId,
        const std::string& paneTitle,
        std::string& error);
}
