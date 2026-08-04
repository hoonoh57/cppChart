from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8-sig", newline="")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return result


# Comparison module sees the explicit 0I contract.
path = "app/comparison_module.h"
text = read(path)
text = replace_once(
    text,
    "#include \"../core/kiwoom_market_data.h\"\n",
    "#include \"../core/kiwoom_market_data.h\"\n"
    "#include \"../core/kiwoom_index_realtime.h\"\n",
    "comparison index include")
text = text.replace(
    "        const ComparisonDefinition* FindDefinition(\n",
    "        bool FindDefinition(\n")
write(path, text)

path = "app/comparison_module.cpp"
text = read(path)
text = text.replace(
    "    const ComparisonDefinition* ComparisonModule::FindDefinition(\n",
    "    bool ComparisonModule::FindDefinition(\n")
text = text.replace(
    "            return &copy;\n",
    "            return true;\n")
text = text.replace(
    "        return nullptr;\n    }\n\n    bool ComparisonModule::ValidateDefinition",
    "        return false;\n    }\n\n    bool ComparisonModule::ValidateDefinition")
write(path, text)

# Fix std::string property editing without depending on imgui_stdlib.
path = "ui/comparison_manager_ui.cpp"
text = read(path)
old = r'''        if (ImGui::InputText(
                "표시명",
                &state.draft.displayName))
        {
            state.dirty = true;
        }
'''
new = r'''        char displayName[128]{};
        std::snprintf(
            displayName,
            sizeof(displayName),
            "%s",
            state.draft.displayName.c_str());
        if (ImGui::InputText(
                "표시명",
                displayName,
                sizeof(displayName)))
        {
            state.draft.displayName = displayName;
            state.dirty = true;
        }
'''
text = replace_once(text, old, new, "comparison display-name input")
old = r'''            if (ImGui::InputText(
                    "패널 ID",
                    &state.draft.paneId))
            {
                state.dirty = true;
            }
            if (ImGui::InputText(
                    "패널 제목",
                    &state.draft.paneTitle))
            {
                state.dirty = true;
            }
'''
new = r'''            char paneId[128]{};
            char paneTitle[128]{};
            std::snprintf(
                paneId,
                sizeof(paneId),
                "%s",
                state.draft.paneId.c_str());
            std::snprintf(
                paneTitle,
                sizeof(paneTitle),
                "%s",
                state.draft.paneTitle.c_str());
            if (ImGui::InputText(
                    "패널 ID",
                    paneId,
                    sizeof(paneId)))
            {
                state.draft.paneId = paneId;
                state.dirty = true;
            }
            if (ImGui::InputText(
                    "패널 제목",
                    paneTitle,
                    sizeof(paneTitle)))
            {
                state.draft.paneTitle = paneTitle;
                state.dirty = true;
            }
'''
text = replace_once(text, old, new, "comparison pane text input")
write(path, text)

# Runtime runner: multiple 0B symbols plus multiple 0I indices.
path = "platform/kiwoom_runtime_runner.h"
text = read(path)
text = replace_once(
    text,
    "#include \"../core/kiwoom_market_data.h\"\n",
    "#include \"../core/kiwoom_market_data.h\"\n"
    "#include \"../core/kiwoom_index_realtime.h\"\n",
    "runner index include")
text = replace_once(
    text,
    "#include <mutex>\n#include <string>\n",
    "#include <mutex>\n#include <set>\n#include <string>\n",
    "runner set include")
text = replace_once(
    text,
    "        std::function<void(const StockTradeTick&)> stockTrade;\n",
    "        std::function<void(const StockTradeTick&)> stockTrade;\n"
    "        std::function<void(const IndexValueTick&)> indexValue;\n",
    "index callback")
text = replace_once(
    text,
    "        bool UnsubscribeStockTrades(\n"
    "            const std::string& stockCode,\n"
    "            std::string& error);\n",
    "        bool UnsubscribeStockTrades(\n"
    "            const std::string& stockCode,\n"
    "            std::string& error);\n\n"
    "        bool SubscribeIndexValues(\n"
    "            const std::string& indexCode,\n"
    "            std::string& error);\n\n"
    "        bool UnsubscribeIndexValues(\n"
    "            const std::string& indexCode,\n"
    "            std::string& error);\n",
    "index subscription API")
text = text.replace(
    "        void TryQueueStockTradeSubscription();\n",
    "        void TryQueueRealtimeSubscriptions();\n")
text = replace_once(
    text,
    "        std::string stockTradeCode_;\n"
    "        bool stockTradeSubscriptionSent_ = false;\n",
    "        std::set<std::string> stockTradeCodes_;\n"
    "        std::set<std::string> stockTradeSubscriptionsSent_;\n"
    "        std::set<std::string> indexValueCodes_;\n"
    "        std::set<std::string> indexValueSubscriptionsSent_;\n",
    "multi subscription state")
write(path, text)

path = "platform/kiwoom_runtime_runner.cpp"
text = read(path)
text = text.replace(
    "            stockTradeCode_.clear();\n"
    "            stockTradeSubscriptionSent_ = false;\n",
    "            stockTradeCodes_.clear();\n"
    "            stockTradeSubscriptionsSent_.clear();\n"
    "            indexValueCodes_.clear();\n"
    "            indexValueSubscriptionsSent_.clear();\n")
# Replace stock subscribe/remove implementations.
pattern = (
    r"    bool KiwoomRuntimeRunner::SubscribeStockTrades\(.*?\n"
    r"    bool KiwoomRuntimeRunner::SubmitOrder\(")
replacement = r'''    bool KiwoomRuntimeRunner::SubscribeStockTrades(
        const std::string& stockCode,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }
        if (stockCode.empty()) {
            error = "stock code is required for real-time subscription";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCodes_.insert(stockCode);
        }
        TryQueueRealtimeSubscriptions();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::UnsubscribeStockTrades(
        const std::string& stockCode,
        std::string& error)
    {
        if (stockCode.empty()) {
            error = "stock code is required for real-time removal";
            return false;
        }
        bool removalRequired = false;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            stockTradeCodes_.erase(stockCode);
            removalRequired =
                stockTradeSubscriptionsSent_.erase(stockCode) > 0U;
        }
        if (removalRequired &&
            running_.load(std::memory_order_acquire) &&
            transport_->IsWebSocketConnected())
        {
            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::SendWebSocketText;
            action.text = BuildWebSocketRemovalMessage(
                "2", { stockCode }, { "0B" });
            Enqueue({ std::move(action) });
            Log("WS", "stock trade 0B removal queued: " + stockCode);
        }
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::SubscribeIndexValues(
        const std::string& indexCode,
        std::string& error)
    {
        if (!running_.load(std::memory_order_acquire)) {
            error = "Kiwoom runtime is not running";
            return false;
        }
        if (indexCode.empty()) {
            error = "index code is required for real-time subscription";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            indexValueCodes_.insert(indexCode);
        }
        TryQueueRealtimeSubscriptions();
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::UnsubscribeIndexValues(
        const std::string& indexCode,
        std::string& error)
    {
        if (indexCode.empty()) {
            error = "index code is required for real-time removal";
            return false;
        }
        bool removalRequired = false;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            indexValueCodes_.erase(indexCode);
            removalRequired =
                indexValueSubscriptionsSent_.erase(indexCode) > 0U;
        }
        if (removalRequired &&
            running_.load(std::memory_order_acquire) &&
            transport_->IsWebSocketConnected())
        {
            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::SendWebSocketText;
            action.text = BuildWebSocketRemovalMessage(
                "3", { indexCode }, { "0I" });
            Enqueue({ std::move(action) });
            Log("WS", "index value 0I removal queued: " + indexCode);
        }
        error.clear();
        return true;
    }

    bool KiwoomRuntimeRunner::SubmitOrder('''
text = regex_once(text, pattern, replacement, "runner subscription implementations")
text = text.replace(
    "                stockTradeSubscriptionSent_ = false;\n",
    "                stockTradeSubscriptionsSent_.clear();\n"
    "                indexValueSubscriptionsSent_.clear();\n")
text = text.replace(
    "                TryQueueStockTradeSubscription();\n",
    "                TryQueueRealtimeSubscriptions();\n")
text = text.replace(
    "        TryQueueStockTradeSubscription();\n",
    "        TryQueueRealtimeSubscriptions();\n")
# Receiver handles 0B and 0I.
old = r'''                    for (const RealTimeRecord& record : envelope.records) {
                        if (record.type != "0B") continue;

                        const StockTradeDecodeResult decoded =
                            DecodeStockTradeRecord(record);
                        if (!decoded.result.ok) {
                            Log(
                                "FAULT",
                                decoded.result.error.empty()
                                    ? "stock trade 0B decode failed"
                                    : decoded.result.error);
                            continue;
                        }
                        if (callbacks_.stockTrade) {
                            callbacks_.stockTrade(decoded.tick);
                        }
                    }
'''
new = r'''                    for (const RealTimeRecord& record : envelope.records) {
                        if (record.type == "0B") {
                            const StockTradeDecodeResult decoded =
                                DecodeStockTradeRecord(record);
                            if (!decoded.result.ok) {
                                Log(
                                    "FAULT",
                                    decoded.result.error.empty()
                                        ? "stock trade 0B decode failed"
                                        : decoded.result.error);
                                continue;
                            }
                            if (callbacks_.stockTrade) {
                                callbacks_.stockTrade(decoded.tick);
                            }
                        }
                        else if (record.type == "0I") {
                            const IndexValueDecodeResult decoded =
                                DecodeIndexValueRecord(record);
                            if (!decoded.result.ok) {
                                Log(
                                    "FAULT",
                                    decoded.result.error.empty()
                                        ? "index value 0I decode failed"
                                        : decoded.result.error);
                                continue;
                            }
                            if (callbacks_.indexValue) {
                                callbacks_.indexValue(decoded.tick);
                            }
                        }
                    }
'''
text = replace_once(text, old, new, "runner realtime decoding")
# Replace queue function.
pattern = (
    r"    void KiwoomRuntimeRunner::TryQueueStockTradeSubscription\(\)\n"
    r"    \{.*?\n    \}\n\n"
    r"    void KiwoomRuntimeRunner::StartReceiver")
replacement = r'''    void KiwoomRuntimeRunner::TryQueueRealtimeSubscriptions()
    {
        if (!running_.load(std::memory_order_acquire)) return;
        if (!engine_.Snapshot().orderSubmissionAllowed) return;
        if (!transport_->IsWebSocketConnected()) return;

        std::vector<std::string> stocks;
        std::vector<std::string> indices;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            for (const std::string& code : stockTradeCodes_) {
                if (stockTradeSubscriptionsSent_.insert(code).second) {
                    stocks.push_back(code);
                }
            }
            for (const std::string& code : indexValueCodes_) {
                if (indexValueSubscriptionsSent_.insert(code).second) {
                    indices.push_back(code);
                }
            }
        }

        if (!stocks.empty()) {
            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::SendWebSocketText;
            action.text = BuildWebSocketRegistrationMessage(
                "2", true, stocks, { "0B" });
            Enqueue({ std::move(action) });
            Log("WS", "stock trade 0B subscriptions queued: " +
                std::to_string(stocks.size()));
        }
        if (!indices.empty()) {
            KiwoomRuntimeAction action;
            action.type = KiwoomRuntimeActionType::SendWebSocketText;
            action.text = BuildWebSocketRegistrationMessage(
                "3", true, indices, { "0I" });
            Enqueue({ std::move(action) });
            Log("WS", "index value 0I subscriptions queued: " +
                std::to_string(indices.size()));
        }
    }

    void KiwoomRuntimeRunner::StartReceiver'''
text = regex_once(text, pattern, replacement, "multi realtime queue")
write(path, text)

# Chart workspace accepts comparison contributions after indicators.
path = "app/chart_workspace_module.h"
text = read(path)
text = replace_once(
    text,
    "    class IndicatorRenderAdapter;\n",
    "    class IndicatorRenderAdapter;\n"
    "    struct ComparisonModuleSnapshot;\n"
    "    class ComparisonRenderAdapter;\n",
    "comparison forward declarations")
text = replace_once(
    text,
    "        std::uint64_t indicatorRevision = 0;\n",
    "        std::uint64_t indicatorRevision = 0;\n"
    "        std::uint64_t comparisonRevision = 0;\n",
    "workspace comparison revision")
anchor = r'''        bool UpdateMarketChart(
            const std::string& workspaceId,
            const std::string& title,
            const std::string& seriesId,
            const ChartMarketSource& source,
            const IndicatorModuleSnapshot& indicatorSnapshot,
            IndicatorRenderAdapter& indicatorAdapter,
            std::string& error);
'''
new_anchor = anchor + r'''
        bool UpdateMarketChart(
            const std::string& workspaceId,
            const std::string& title,
            const std::string& seriesId,
            const ChartMarketSource& source,
            const IndicatorModuleSnapshot& indicatorSnapshot,
            IndicatorRenderAdapter& indicatorAdapter,
            const ComparisonModuleSnapshot& comparisonSnapshot,
            ComparisonRenderAdapter& comparisonAdapter,
            std::string& error);
'''
text = replace_once(text, anchor, new_anchor, "workspace comparison overload")
anchor = r'''        bool NeedsUpdate(
            std::uint64_t sourceRevision,
            const IndicatorModuleSnapshot& indicatorSnapshot,
            const IndicatorRenderAdapter& indicatorAdapter) const noexcept;
'''
new_anchor = anchor + r'''

        bool NeedsUpdate(
            std::uint64_t sourceRevision,
            const IndicatorModuleSnapshot& indicatorSnapshot,
            const IndicatorRenderAdapter& indicatorAdapter,
            const ComparisonModuleSnapshot& comparisonSnapshot,
            const ComparisonRenderAdapter& comparisonAdapter) const noexcept;
'''
text = replace_once(text, anchor, new_anchor, "workspace comparison needs update")
text = replace_once(
    text,
    "            IndicatorRenderAdapter* indicatorAdapter,\n"
    "            std::string& error);\n",
    "            IndicatorRenderAdapter* indicatorAdapter,\n"
    "            const ComparisonModuleSnapshot* comparisonSnapshot,\n"
    "            ComparisonRenderAdapter* comparisonAdapter,\n"
    "            std::string& error);\n",
    "workspace core comparison args")
text = replace_once(
    text,
    "        static std::size_t CountSeries(\n",
    "        static std::uint64_t ComparisonCompositeRevision(\n"
    "            const ComparisonModuleSnapshot& snapshot,\n"
    "            const ComparisonRenderAdapter& adapter) noexcept;\n\n"
    "        static std::size_t CountSeries(\n",
    "comparison revision helper")
text = replace_once(
    text,
    "        std::uint64_t indicatorRevision_ = 0;\n",
    "        std::uint64_t indicatorRevision_ = 0;\n"
    "        std::uint64_t comparisonRevision_ = 0;\n",
    "workspace comparison member")
write(path, text)

path = "app/chart_workspace_module.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"indicator_render_adapter.h\"\n",
    "#include \"indicator_render_adapter.h\"\n"
    "#include \"comparison_render_adapter.h\"\n",
    "workspace comparison include")
text = replace_once(
    text,
    "            indicatorRevision_ = 0;\n",
    "            indicatorRevision_ = 0;\n"
    "            comparisonRevision_ = 0;\n",
    "workspace off comparison reset")
# Existing calls pass null comparison args.
text = text.replace(
    "            nullptr,\n            nullptr,\n            error);",
    "            nullptr,\n            nullptr,\n            nullptr,\n            nullptr,\n            error);")
text = text.replace(
    "            &indicatorSnapshot,\n            &indicatorAdapter,\n            error);",
    "            &indicatorSnapshot,\n            &indicatorAdapter,\n            nullptr,\n            nullptr,\n            error);")
# Insert full overload before core.
marker = "    bool ChartWorkspaceModule::UpdateMarketChartCore(\n"
overload = r'''    bool ChartWorkspaceModule::UpdateMarketChart(
        const std::string& workspaceId,
        const std::string& title,
        const std::string& seriesId,
        const ChartMarketSource& source,
        const IndicatorModuleSnapshot& indicatorSnapshot,
        IndicatorRenderAdapter& indicatorAdapter,
        const ComparisonModuleSnapshot& comparisonSnapshot,
        ComparisonRenderAdapter& comparisonAdapter,
        std::string& error)
    {
        return UpdateMarketChartCore(
            workspaceId,
            title,
            seriesId,
            source,
            &indicatorSnapshot,
            &indicatorAdapter,
            &comparisonSnapshot,
            &comparisonAdapter,
            error);
    }

'''
text = replace_once(text, marker, overload + marker, "workspace full comparison overload")
text = replace_once(
    text,
    "        IndicatorRenderAdapter* indicatorAdapter,\n"
    "        std::string& error)\n",
    "        IndicatorRenderAdapter* indicatorAdapter,\n"
    "        const ComparisonModuleSnapshot* comparisonSnapshot,\n"
    "        ComparisonRenderAdapter* comparisonAdapter,\n"
    "        std::string& error)\n",
    "workspace core signature")
text = replace_once(
    text,
    "        if (\n"
    "            (indicatorSnapshot == nullptr) !=\n"
    "            (indicatorAdapter == nullptr))\n"
    "        {\n"
    "            error = \"indicator snapshot and adapter must be supplied together\";\n"
    "            return false;\n"
    "        }\n",
    "        if ((indicatorSnapshot == nullptr) != (indicatorAdapter == nullptr)) {\n"
    "            error = \"indicator snapshot and adapter must be supplied together\";\n"
    "            return false;\n"
    "        }\n"
    "        if ((comparisonSnapshot == nullptr) != (comparisonAdapter == nullptr)) {\n"
    "            error = \"comparison snapshot and adapter must be supplied together\";\n"
    "            return false;\n"
    "        }\n",
    "workspace paired comparison validation")
text = replace_once(
    text,
    "        std::uint64_t nextDocumentRevision = 0;\n",
    "        const std::uint64_t nextComparisonRevision =\n"
    "            comparisonSnapshot != nullptr\n"
    "                ? ComparisonCompositeRevision(\n"
    "                    *comparisonSnapshot,\n"
    "                    *comparisonAdapter)\n"
    "                : 0;\n"
    "        std::uint64_t nextDocumentRevision = 0;\n",
    "workspace comparison composite")
text = text.replace(
    "                indicatorRevision_ == nextIndicatorRevision &&\n"
    "                document_ != nullptr)",
    "                indicatorRevision_ == nextIndicatorRevision &&\n"
    "                comparisonRevision_ == nextComparisonRevision &&\n"
    "                document_ != nullptr)")
# Apply comparison before validation.
text = replace_once(
    text,
    "        std::string validationError;\n",
    "        if (comparisonSnapshot != nullptr &&\n"
    "            IsVisibleLevel(comparisonSnapshot->level))\n"
    "        {\n"
    "            std::string contributionError;\n"
    "            if (!comparisonAdapter->Apply(\n"
    "                    *comparisonSnapshot,\n"
    "                    candidate,\n"
    "                    contributionError))\n"
    "            {\n"
    "                SetError(\n"
    "                    \"comparison render contribution failed: \" +\n"
    "                    contributionError);\n"
    "                error = contributionError;\n"
    "                return false;\n"
    "            }\n"
    "        }\n\n"
    "        std::string validationError;\n",
    "workspace comparison apply")
text = replace_once(
    text,
    "            indicatorRevision_ = nextIndicatorRevision;\n",
    "            indicatorRevision_ = nextIndicatorRevision;\n"
    "            comparisonRevision_ = nextComparisonRevision;\n",
    "workspace comparison commit")
text = replace_once(
    text,
    "        result.indicatorRevision = indicatorRevision_;\n",
    "        result.indicatorRevision = indicatorRevision_;\n"
    "        result.comparisonRevision = comparisonRevision_;\n",
    "workspace comparison snapshot")
# Add full NeedsUpdate overload.
marker = "    const char* ChartWorkspaceModule::StateName(\n"
needs = r'''    bool ChartWorkspaceModule::NeedsUpdate(
        std::uint64_t sourceRevision,
        const IndicatorModuleSnapshot& indicatorSnapshot,
        const IndicatorRenderAdapter& indicatorAdapter,
        const ComparisonModuleSnapshot& comparisonSnapshot,
        const ComparisonRenderAdapter& comparisonAdapter) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return
            IsVisibleLevel(level_) &&
            (document_ == nullptr ||
             state_ != ChartWorkspaceState::Ready ||
             sourceRevision_ != sourceRevision ||
             indicatorRevision_ != IndicatorCompositeRevision(
                 indicatorSnapshot, indicatorAdapter) ||
             comparisonRevision_ != ComparisonCompositeRevision(
                 comparisonSnapshot, comparisonAdapter));
    }

'''
text = replace_once(text, marker, needs + marker, "workspace full needs update")
# Add revision helper before CountSeries.
marker = "    std::size_t ChartWorkspaceModule::CountSeries(\n"
helper = r'''    std::uint64_t ChartWorkspaceModule::ComparisonCompositeRevision(
        const ComparisonModuleSnapshot& snapshot,
        const ComparisonRenderAdapter& adapter) noexcept
    {
        std::uint64_t seed = snapshot.revision;
        const std::uint64_t adapterRevision = adapter.Revision();
        seed ^=
            adapterRevision + 0x9e3779b97f4a7c15ULL +
            (seed << 6U) + (seed >> 2U);
        const std::uint64_t level =
            static_cast<std::uint64_t>(snapshot.level);
        seed ^=
            level + 0x9e3779b97f4a7c15ULL +
            (seed << 6U) + (seed >> 2U);
        return seed;
    }

'''
text = replace_once(text, marker, helper + marker, "comparison revision implementation")
write(path, text)

# Production shell composition and routing.
path = "shell_main.cpp"
text = read(path)
text = replace_once(
    text,
    "#include \"app/indicator_workspace_coordinator.h\"\n",
    "#include \"app/indicator_workspace_coordinator.h\"\n"
    "#include \"app/comparison_module.h\"\n"
    "#include \"app/comparison_render_adapter.h\"\n",
    "shell comparison includes")
text = replace_once(
    text,
    "#include \"ui/indicator_manager_ui.h\"\n",
    "#include \"ui/indicator_manager_ui.h\"\n"
    "#include \"ui/comparison_manager_ui.h\"\n",
    "shell comparison UI include")
text = replace_once(
    text,
    "static trading::ui::IndicatorManagerUiState g_indicatorManagerUi;\n",
    "static trading::ui::IndicatorManagerUiState g_indicatorManagerUi;\n"
    "static trading::app::ComparisonModule g_comparisonModule;\n"
    "static trading::app::ComparisonRenderAdapter g_comparisonRenderAdapter;\n"
    "static std::vector<trading::app::ComparisonDefinition>\n"
    "    g_comparisonDefinitions;\n"
    "static trading::ui::ComparisonManagerUiState g_comparisonManagerUi;\n",
    "shell comparison globals")
# Feature registry.
text = replace_once(
    text,
    "    if (!g_featureRegistry.Register(\n"
    "            \"chart-workspace\",\n",
    "    if (!g_featureRegistry.Register(\n"
    "            \"comparison\",\n"
    "            \"Comparison Series\",\n"
    "            trading::app::FeatureLevel::Visible,\n"
    "            { \"market-data\" },\n"
    "            error)) return false;\n"
    "    if (!g_featureRegistry.Register(\n"
    "            \"chart-workspace\",\n",
    "comparison feature registration")
# SetFeatureLevel comparison branch before chart.
text = replace_once(
    text,
    "    else if (id == \"chart-workspace\") {\n",
    "    else if (id == \"comparison\") {\n"
    "        if (!g_comparisonModule.SetLevel(level, error)) {\n"
    "            std::string rollbackError;\n"
    "            g_featureRegistry.SetLevel(id, previousFeature.level, rollbackError);\n"
    "            return false;\n"
    "        }\n"
    "        if (level == trading::app::FeatureLevel::Off) {\n"
    "            g_comparisonRenderAdapter.ClearCache();\n"
    "        }\n"
    "    }\n"
    "    else if (id == \"chart-workspace\") {\n",
    "comparison feature level")
# Add application callbacks before Kiwoom label.
marker = "static const char* KiwoomSessionStateLabel(\n"
callbacks = r'''static bool ApplyComparisonDefinitions(
    const std::vector<trading::app::ComparisonDefinition>& candidate,
    std::string& error)
{
    const std::vector<trading::app::ComparisonDefinition> previous =
        g_comparisonDefinitions;
    if (!g_comparisonModule.Configure(candidate, error)) return false;

    auto used = [](const std::vector<trading::app::ComparisonDefinition>& values,
                   trading::app::ComparisonInstrumentKind kind,
                   const std::string& code) {
        for (const auto& value : values) {
            if (value.visible && value.kind == kind && value.code == code) {
                return true;
            }
        }
        return false;
    };

    if (g_runtimeRunner) {
        for (const auto& old : previous) {
            if (!old.visible || used(candidate, old.kind, old.code)) continue;
            std::string ignored;
            if (old.kind == trading::app::ComparisonInstrumentKind::Stock) {
                g_runtimeRunner->UnsubscribeStockTrades(old.code, ignored);
            }
            else {
                g_runtimeRunner->UnsubscribeIndexValues(old.code, ignored);
            }
        }
        const trading::app::ComparisonModuleSnapshot snapshot =
            g_comparisonModule.Snapshot();
        for (const auto& series : snapshot.series) {
            if (!series.definition.visible ||
                series.state != trading::app::ComparisonSeriesState::Ready)
            {
                continue;
            }
            std::string ignored;
            if (series.definition.kind ==
                trading::app::ComparisonInstrumentKind::Stock)
            {
                g_runtimeRunner->SubscribeStockTrades(
                    series.definition.code, ignored);
            }
            else {
                g_runtimeRunner->SubscribeIndexValues(
                    series.definition.code, ignored);
            }
        }
    }

    g_mainRenderSurface.dirty = true;
    std::string healthError;
    g_featureRegistry.SetHealth("comparison", true, {}, healthError);
    WakeFrames(6);
    error.clear();
    return true;
}

static bool RequestComparisonData(
    const std::string& comparisonId,
    std::string& error)
{
    if (!g_runtimeRunner || !g_runtimeRunner->IsRunning()) {
        error = "키움 런타임이 실행 중이 아닙니다.";
        return false;
    }
    trading::app::ComparisonDefinition definition;
    if (!g_comparisonModule.FindDefinition(
            comparisonId, definition))
    {
        error = "비교 시계열을 찾지 못했습니다.";
        return false;
    }
    const trading::app::MarketDataSnapshot market =
        g_marketDataModule.Snapshot();
    const int minuteUnit = market.minuteUnit > 0
        ? market.minuteUnit
        : MinuteUnitFromSelection(g_timeFrameIndex);
    if (!g_comparisonModule.BeginRequest(
            comparisonId, minuteUnit, error))
    {
        return false;
    }
    const bool requested =
        definition.kind == trading::app::ComparisonInstrumentKind::Stock
            ? g_runtimeRunner->RequestStockMinuteBars(
                definition.code, minuteUnit, {}, error)
            : g_runtimeRunner->RequestIndexMinuteBars(
                definition.code, minuteUnit, {}, error);
    if (!requested) {
        g_comparisonModule.SetError(comparisonId, error);
        return false;
    }
    g_log.Add(
        "DATA",
        "비교 %s 분봉 요청: %s %d분",
        definition.kind == trading::app::ComparisonInstrumentKind::Stock
            ? "종목" : "지수",
        definition.code.c_str(),
        minuteUnit);
    return true;
}

'''
text = replace_once(text, marker, callbacks + marker, "shell comparison callbacks")
# DrawMarketDataPanel: comparison snapshot and full workspace overload.
text = replace_once(
    text,
    "    bool chartNeedsUpdate = false;\n",
    "    trading::app::ComparisonModuleSnapshot comparisonSnapshot =\n"
    "        g_comparisonModule.Snapshot();\n"
    "    trading::app::IndicatorModuleSnapshot workspaceIndicator =\n"
    "        indicatorSnapshot;\n"
    "    if (!useIndicators) {\n"
    "        workspaceIndicator.level = trading::app::FeatureLevel::Off;\n"
    "    }\n\n"
    "    bool chartNeedsUpdate = false;\n",
    "comparison workspace snapshot")
pattern = (
    r"    bool chartNeedsUpdate = false;\n.*?\n"
    r"    if \(chartNeedsUpdate\) \{.*?\n"
    r"    \}\n\n"
    r"    const trading::app::ChartWorkspaceSnapshot workspace =")
replacement = r'''    bool chartNeedsUpdate = g_chartWorkspaceModule.NeedsUpdate(
        marketSeries.revision,
        workspaceIndicator,
        g_indicatorRenderAdapter,
        comparisonSnapshot,
        g_comparisonRenderAdapter);

    if (chartNeedsUpdate) {
        std::string chartError;
        const bool updated = g_chartWorkspaceModule.UpdateMarketChart(
            "main-market-chart",
            snapshot.code,
            snapshot.code,
            chartSource,
            workspaceIndicator,
            g_indicatorRenderAdapter,
            comparisonSnapshot,
            g_comparisonRenderAdapter,
            chartError);
        if (!updated) {
            g_log.Add(
                "FAULT",
                "차트 워크스페이스 갱신 실패: %s",
                chartError.c_str());
            std::string healthError;
            g_featureRegistry.SetHealth(
                "chart-workspace", false, chartError, healthError);
            ImGui::End();
            return;
        }
        g_mainRenderSurface.dirty = true;
    }

    const trading::app::ChartWorkspaceSnapshot workspace ='''
text = regex_once(text, pattern, replacement, "shell comparison workspace composition")
# Selection handoff to indicator or comparison.
old = r'''    if (g_mainRenderSurface.selectionChanged) {
        trading::ui::SelectIndicator(
            g_indicatorManagerUi,
            g_mainRenderSurface.selectedOwnerId,
            g_mainRenderSurface.selectionDoubleClicked);
        WakeFrames(4);
    }
'''
new = r'''    if (g_mainRenderSurface.selectionChanged) {
        if (trading::app::FindIndicatorDefinition(
                g_indicatorDefinitions,
                g_mainRenderSurface.selectedOwnerId) != nullptr)
        {
            trading::ui::SelectIndicator(
                g_indicatorManagerUi,
                g_mainRenderSurface.selectedOwnerId,
                g_mainRenderSurface.selectionDoubleClicked);
        }
        else {
            trading::app::ComparisonDefinition comparison;
            if (g_comparisonModule.FindDefinition(
                    g_mainRenderSurface.selectedOwnerId,
                    comparison))
            {
                trading::ui::SelectComparison(
                    g_comparisonManagerUi,
                    comparison.id,
                    g_mainRenderSurface.selectionDoubleClicked);
            }
        }
        WakeFrames(4);
    }
'''
text = replace_once(text, old, new, "comparison selection handoff")
# Dock and draw comparison window.
text = replace_once(
    text,
    "    ImGui::DockBuilderDockWindow(\"프로퍼티\", right);\n",
    "    ImGui::DockBuilderDockWindow(\"프로퍼티\", right);\n"
    "    ImGui::DockBuilderDockWindow(\"비교\", right);\n",
    "comparison dock")
text = replace_once(
    text,
    "        DrawDashboard();\n",
    "        trading::ui::DrawComparisonManagerWindow(\n"
    "            g_comparisonDefinitions,\n"
    "            g_comparisonModule.Snapshot(),\n"
    "            g_comparisonManagerUi,\n"
    "            ApplyComparisonDefinitions,\n"
    "            RequestComparisonData);\n"
    "        DrawDashboard();\n",
    "draw comparison manager")
# Runtime callbacks route stock/index minute data and realtime.
pattern = (
    r"        callbacks\.minuteBars = \[\]\(\n"
    r"            const trading::MinuteBarsPage& page,\n"
    r"            const trading::Continuation& continuation\) \{.*?\n"
    r"        \};\n"
    r"        callbacks\.stockTrade = \[\]\(\n"
    r"            const trading::StockTradeTick& tick\) \{.*?\n"
    r"        \};")
replacement = r'''        callbacks.minuteBars = [](
            const trading::MinuteBarsPage& page,
            const trading::Continuation& continuation) {
            const double started = NowSeconds();
            const trading::app::MarketDataSnapshot before =
                g_marketDataModule.Snapshot();
            trading::app::MarketDataApplyResult marketApplied;
            if (page.instrument == trading::MinuteBarInstrument::Stock &&
                !before.code.empty() && before.code == page.code)
            {
                marketApplied =
                    g_marketDataModule.ApplyMinuteBars(page, continuation);
            }
            const trading::app::ComparisonApplyResult comparisonApplied =
                g_comparisonModule.ApplyMinuteBars(page, continuation);
            const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
                (NowSeconds() - started) * 1000000.0);
            const trading::app::MarketDataSnapshot market =
                g_marketDataModule.Snapshot();
            const trading::app::ComparisonModuleSnapshot comparison =
                g_comparisonModule.Snapshot();
            RecordFeatureWork(
                "market-data",
                elapsedMicros,
                market.retainedBytes,
                market.code.empty() ? 0 : 1,
                market.hasLatestBar ? 2 : 0,
                0,
                marketApplied.stale ? 1 : 0);
            RecordFeatureWork(
                "comparison",
                elapsedMicros,
                comparison.retainedBytes +
                    g_comparisonRenderAdapter.RetainedBytes(),
                comparison.series.size(),
                comparison.series.size(),
                0,
                comparisonApplied.stale ? 1 : 0);

            if (marketApplied.applied) {
                g_tradingState.UpdateCurrentPrice(
                    marketApplied.code,
                    marketApplied.latestPriceWon);
                std::string subscriptionError;
                if (g_runtimeRunner &&
                    g_runtimeRunner->SubscribeStockTrades(
                        page.code, subscriptionError))
                {
                    g_marketDataModule.SetStockTradeSubscriptionRequested(true);
                }
            }
            if (comparisonApplied.applied) {
                trading::app::ComparisonDefinition definition;
                if (g_comparisonModule.FindDefinition(
                        comparisonApplied.comparisonId,
                        definition))
                {
                    std::string subscriptionError;
                    const bool subscribed = definition.kind ==
                        trading::app::ComparisonInstrumentKind::Stock
                            ? g_runtimeRunner &&
                                g_runtimeRunner->SubscribeStockTrades(
                                    definition.code, subscriptionError)
                            : g_runtimeRunner &&
                                g_runtimeRunner->SubscribeIndexValues(
                                    definition.code, subscriptionError);
                    if (!subscribed) {
                        g_log.Add(
                            "FAULT",
                            "비교 실시간 등록 실패: %s",
                            subscriptionError.c_str());
                    }
                }
            }
            if (!marketApplied.applied && !comparisonApplied.applied &&
                !marketApplied.stale && !comparisonApplied.stale)
            {
                const std::string error = !marketApplied.error.empty()
                    ? marketApplied.error
                    : comparisonApplied.error;
                if (!error.empty()) {
                    g_log.Add("FAULT", "분봉 적용 실패: %s", error.c_str());
                }
            }
            WakeFrames(4);
        };
        callbacks.stockTrade = [](
            const trading::StockTradeTick& tick) {
            const double started = NowSeconds();
            const trading::app::MarketDataApplyResult marketApplied =
                g_marketDataModule.ApplyStockTradeTick(tick);
            const trading::app::ComparisonApplyResult comparisonApplied =
                g_comparisonModule.ApplyStockTradeTick(tick);
            const std::uint64_t elapsedMicros = static_cast<std::uint64_t>(
                (NowSeconds() - started) * 1000000.0);
            if (marketApplied.applied) {
                g_tradingState.UpdateCurrentPrice(
                    marketApplied.code,
                    marketApplied.latestPriceWon);
            }
            if (marketApplied.applied || comparisonApplied.applied) {
                WakeFrames(2);
            }
            else if (!marketApplied.stale && !comparisonApplied.stale) {
                const std::string error = !marketApplied.error.empty()
                    ? marketApplied.error
                    : comparisonApplied.error;
                if (!error.empty()) {
                    g_log.Add("FAULT", "0B 병합 실패: %s", error.c_str());
                }
            }
            RecordFeatureWork(
                "comparison",
                elapsedMicros,
                g_comparisonModule.Snapshot().retainedBytes,
                g_comparisonModule.Snapshot().series.size(),
                g_comparisonModule.Snapshot().series.size(),
                0,
                comparisonApplied.stale ? 1 : 0);
        };
        callbacks.indexValue = [](
            const trading::IndexValueTick& tick) {
            const trading::app::ComparisonApplyResult applied =
                g_comparisonModule.ApplyIndexValueTick(tick);
            if (applied.applied) {
                WakeFrames(2);
            }
            else if (!applied.stale && !applied.error.empty()) {
                g_log.Add("FAULT", "0I 지수 병합 실패: %s", applied.error.c_str());
            }
        };'''
text = regex_once(text, pattern, replacement, "shell comparison data routing")
write(path, text)

# Build and tests.
path = "build.bat"
text = read(path)
text = replace_once(
    text,
    "   core\\kiwoom_market_data.cpp ^\n",
    "   core\\kiwoom_market_data.cpp ^\n"
    "   core\\kiwoom_index_realtime.cpp ^\n",
    "build index realtime")
text = replace_once(
    text,
    "   app\\indicator_workspace_coordinator.cpp ^\n",
    "   app\\indicator_workspace_coordinator.cpp ^\n"
    "   app\\comparison_module.cpp ^\n"
    "   app\\comparison_render_adapter.cpp ^\n",
    "build comparison module")
text = replace_once(
    text,
    "   ui\\indicator_manager_ui.cpp ^\n",
    "   ui\\indicator_manager_ui.cpp ^\n"
    "   ui\\comparison_manager_ui.cpp ^\n",
    "build comparison UI")
write(path, text)

path = "tests/run_all.bat"
text = read(path)
text = replace_once(
    text,
    "call :build_and_run feature_registry_tests.exe",
    "call :build_and_run kiwoom_index_realtime_tests.exe \"tests\\kiwoom_index_realtime_tests.cpp core\\json_lite.cpp core\\kiwoom_protocol.cpp core\\kiwoom_index_realtime.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run comparison_module_tests.exe \"tests\\comparison_module_tests.cpp core\\json_lite.cpp core\\kiwoom_protocol.cpp core\\kiwoom_market_data.cpp core\\kiwoom_index_realtime.cpp app\\comparison_module.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run comparison_render_adapter_tests.exe \"tests\\comparison_render_adapter_tests.cpp app\\comparison_render_adapter.cpp render\\render_document.cpp\"\n"
    "if errorlevel 1 exit /b 1\n\n"
    "call :build_and_run feature_registry_tests.exe",
    "comparison tests")
text = text.replace(
    "app\\indicator_render_adapter.cpp app\\chart_workspace_module.cpp",
    "app\\indicator_render_adapter.cpp app\\comparison_render_adapter.cpp app\\chart_workspace_module.cpp")
text = text.replace(
    "core\\kiwoom_realtime_subscription.cpp platform\\kiwoom_runtime_runner.cpp",
    "core\\kiwoom_realtime_subscription.cpp core\\kiwoom_index_realtime.cpp platform\\kiwoom_runtime_runner.cpp")
write(path, text)

print("comparison runtime applied")
