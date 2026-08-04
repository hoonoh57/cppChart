from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_text(relative: str):
    path = ROOT / relative
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    return path, raw.decode("utf-8-sig"), bom


def write_text(path: Path, text: str, bom: bool):
    payload = text.encode("utf-8")
    if bom:
        payload = b"\xef\xbb\xbf" + payload
    path.write_bytes(payload)


def replace_token(relative: str, old: str, new: str, minimum: int = 1):
    path, text, bom = read_text(relative)
    count = text.count(old)
    if count < minimum:
        raise RuntimeError(
            f"{relative}: expected at least {minimum} occurrence(s), found {count}: {old!r}"
        )
    write_text(path, text.replace(old, new), bom)


# 1. Kiwoom official realtime contract: 0J is 업종지수.
#    0I is 국제금환산가격 and must never be decoded as an index.
replace_token("core/kiwoom_index_realtime.cpp", "0I", "0J", 2)
replace_token("platform/kiwoom_runtime_runner.cpp", "0I", "0J", 4)
replace_token("shell_main.cpp", "0I", "0J", 1)
replace_token("tests/kiwoom_index_realtime_tests.cpp", "0I", "0J", 4)

for doc in (
    "docs/ARCHITECTURE_CONSTITUTION.md",
    "docs/MODULARIZATION_PLAN.md",
    "docs/SESSION_HANDOFF.md",
):
    path, text, bom = read_text(doc)
    if "0I" in text:
        write_text(path, text.replace("0I", "0J"), bom)


# 2. Runtime runner regression: verify 0J subscribe/delivery/reconnect/removal.
runner_test_path, runner_test, runner_test_bom = read_text(
    "tests/kiwoom_runtime_runner_tests.cpp"
)

old = r'''        int StockTradeRemovalCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"trnm\":\"REMOVE\"") !=
                        std::string::npos &&
                    message.find("\"type\":[\"0B\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\"") ==
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        void PushStockTrade()
'''
new = r'''        int StockTradeRemovalCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"trnm\":\"REMOVE\"") !=
                        std::string::npos &&
                    message.find("\"type\":[\"0B\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\"") ==
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        int IndexValueRegistrationCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"type\":[\"0J\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\":\"1\"") !=
                        std::string::npos &&
                    message.find("\"001\"") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        int IndexValueRemovalCount()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int count = 0;
            for (const std::string& message : sentMessages_) {
                if (
                    message.find("\"trnm\":\"REMOVE\"") !=
                        std::string::npos &&
                    message.find("\"type\":[\"0J\"]") !=
                        std::string::npos &&
                    message.find("\"refresh\"") ==
                        std::string::npos &&
                    message.find("\"001\"") != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

        void PushStockTrade()
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: stock removal insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        void PushStockTrade()
        {
            PushText(
                "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
                "\"type\":\"0B\",\"item\":\"000660\","
                "\"name\":\"주식체결\",\"values\":{"
                "\"20\":\"123701\",\"10\":\"+1584000\","
                "\"15\":\"-3\",\"13\":\"552\"}}]}");
        }

    private:
'''
new = r'''        void PushStockTrade()
        {
            PushText(
                "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
                "\"type\":\"0B\",\"item\":\"000660\","
                "\"name\":\"주식체결\",\"values\":{"
                "\"20\":\"123701\",\"10\":\"+1584000\","
                "\"15\":\"-3\",\"13\":\"552\"}}]}");
        }

        void PushIndexValue()
        {
            PushText(
                "{\"trnm\":\"REAL\",\"return_code\":0,\"data\":[{"
                "\"type\":\"0J\",\"item\":\"001\","
                "\"name\":\"업종지수\",\"values\":{"
                "\"20\":\"123701\",\"10\":\"+2,845.67\","
                "\"15\":\"15\",\"13\":\"124500\"}}]}");
        }

    private:
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: PushIndexValue insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        std::atomic<int> wakeCount{ 0 };
        std::atomic<int> stockTradeCount{ 0 };
        std::atomic<bool> observe{ false };
'''
new = r'''        std::atomic<int> wakeCount{ 0 };
        std::atomic<int> stockTradeCount{ 0 };
        std::atomic<int> indexValueCount{ 0 };
        std::atomic<bool> observe{ false };
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index counter insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        callbacks.stockTrade = [&](const trading::StockTradeTick& tick) {
            if (tick.code == "000660" && tick.priceWon == 1584000) {
                stockTradeCount.fetch_add(1, std::memory_order_relaxed);
            }
        };

        trading::platform::KiwoomRuntimeRunner runner(
'''
new = r'''        callbacks.stockTrade = [&](const trading::StockTradeTick& tick) {
            if (tick.code == "000660" && tick.priceWon == 1584000) {
                stockTradeCount.fetch_add(1, std::memory_order_relaxed);
            }
        };
        callbacks.indexValue = [&](const trading::IndexValueTick& tick) {
            if (tick.code == "001" && tick.value == 284567) {
                indexValueCount.fetch_add(1, std::memory_order_relaxed);
            }
        };

        trading::platform::KiwoomRuntimeRunner runner(
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index callback insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        fake->PushStockTrade();
        WaitUntil(
            [&] { return stockTradeCount.load(std::memory_order_relaxed) == 1; },
            "runner did not decode and deliver 0B stock trade");

        trading::OrderIntent buy;
'''
new = r'''        fake->PushStockTrade();
        WaitUntil(
            [&] { return stockTradeCount.load(std::memory_order_relaxed) == 1; },
            "runner did not decode and deliver 0B stock trade");

        Check(runner.SubscribeIndexValues("001", error),
              "ready runner must accept index value subscription");
        WaitUntil(
            [&] { return fake->IndexValueRegistrationCount() >= 1; },
            "runner did not send 0J index value registration");
        fake->PushIndexValue();
        WaitUntil(
            [&] { return indexValueCount.load(std::memory_order_relaxed) == 1; },
            "runner did not decode and deliver 0J index value");

        trading::OrderIntent buy;
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index subscribe insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");

        Check(runner.UnsubscribeStockTrades("000660", error),
'''
new = r'''        WaitUntil(
            [&] { return fake->StockTradeRegistrationCount() >= 2; },
            "runtime did not restore 0B subscription after reconnect");
        WaitUntil(
            [&] { return fake->IndexValueRegistrationCount() >= 2; },
            "runtime did not restore 0J subscription after reconnect");

        Check(runner.UnsubscribeStockTrades("000660", error),
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index reconnect insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        WaitUntil(
            [&] { return fake->StockTradeRemovalCount() >= 1; },
            "runner did not send 0B REMOVE message");

        const int registrationsBeforeSecondReconnect =
            fake->StockTradeRegistrationCount();
'''
new = r'''        WaitUntil(
            [&] { return fake->StockTradeRemovalCount() >= 1; },
            "runner did not send 0B REMOVE message");
        Check(runner.UnsubscribeIndexValues("001", error),
              "runner must accept index value removal");
        WaitUntil(
            [&] { return fake->IndexValueRemovalCount() >= 1; },
            "runner did not send 0J REMOVE message");

        const int registrationsBeforeSecondReconnect =
            fake->StockTradeRegistrationCount();
        const int indexRegistrationsBeforeSecondReconnect =
            fake->IndexValueRegistrationCount();
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index removal insertion point mismatch")
runner_test = runner_test.replace(old, new)

old = r'''        Check(
            fake->StockTradeRegistrationCount() ==
                registrationsBeforeSecondReconnect,
            "removed 0B subscription must not return after reconnect");

        Check(!observe.load(std::memory_order_acquire),
'''
new = r'''        Check(
            fake->StockTradeRegistrationCount() ==
                registrationsBeforeSecondReconnect,
            "removed 0B subscription must not return after reconnect");
        Check(
            fake->IndexValueRegistrationCount() ==
                indexRegistrationsBeforeSecondReconnect,
            "removed 0J subscription must not return after reconnect");

        Check(!observe.load(std::memory_order_acquire),
'''
if runner_test.count(old) != 1:
    raise RuntimeError("runner test: index post-removal assertion mismatch")
runner_test = runner_test.replace(old, new)

write_text(runner_test_path, runner_test, runner_test_bom)


# 3. Indicator manager UX.
ui_path, ui, ui_bom = read_text("ui/indicator_manager_ui.cpp")

old = r'''        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("지표", preview.c_str())) {
            for (const app::IndicatorInstanceDefinition& definition :
                 definitions)
            {
                const bool isSelected =
                    definition.spec.id == state.selectedIndicatorId;
                const std::string label = DefinitionLabel(definition);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    ResetDraft(state, definition);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("지표 추가")) {
            state.error.clear();
            ImGui::OpenPopup("지표 추가");
        }
'''
new = r'''        bool openAddPopup = false;
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("적용 지표", preview.c_str())) {
            ImGui::TextDisabled("현재 적용된 지표");
            for (const app::IndicatorInstanceDefinition& definition :
                 definitions)
            {
                const bool isSelected =
                    definition.spec.id == state.selectedIndicatorId;
                const std::string label = DefinitionLabel(definition);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    ResetDraft(state, definition);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }

            const std::vector<app::IndicatorCatalogEntry>& catalog =
                app::IndicatorCatalog();
            if (!catalog.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("새 지표 추가");
                for (std::size_t index = 0; index < catalog.size(); ++index) {
                    const std::string label =
                        "+ " + catalog[index].displayName;
                    if (ImGui::Selectable(label.c_str(), false)) {
                        state.addTypeIndex = static_cast<int>(index);
                        openAddPopup = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (openAddPopup) {
            state.error.clear();
            ImGui::OpenPopup("지표 추가");
        }

        if (ImGui::Button("새 지표")) {
            state.error.clear();
            ImGui::OpenPopup("지표 추가");
        }
'''
if ui.count(old) != 1:
    raise RuntimeError("indicator UI: selector block mismatch")
ui = ui.replace(old, new)

old = r'''            if (!ImGui::BeginTable(
                    "indicator_output_styles",
                    7,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX))
'''
new = r'''            const float tableHeight = (std::min)(
                190.0f,
                ImGui::GetTextLineHeightWithSpacing() *
                    static_cast<float>(state.draft.outputs.size() + 1U) +
                    12.0f);
            if (!ImGui::BeginTable(
                    "indicator_output_styles",
                    7,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX |
                        ImGuiTableFlags_ScrollY,
                    ImVec2(0.0f, tableHeight)))
'''
if ui.count(old) != 1:
    raise RuntimeError("indicator UI: output table block mismatch")
ui = ui.replace(old, new)

old = r'''        ImGui::Separator();
        ImGui::Text(
            "%s",
            app::IndicatorLegendLabel(state.draft.spec).c_str());
'''
new = r'''        const float editorFooterHeight =
            ImGui::GetFrameHeightWithSpacing() * 2.0f +
            ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild(
            "##indicator_editor_scroll",
            ImVec2(0.0f, -editorFooterHeight),
            false,
            ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::Separator();
        ImGui::Text(
            "%s",
            app::IndicatorLegendLabel(state.draft.spec).c_str());
'''
if ui.count(old) != 1:
    raise RuntimeError("indicator UI: editor child start mismatch")
ui = ui.replace(old, new)

old = r'''        DrawOutputStyles(definitions, state);
        DrawPaneSettings(state.draft, state.dirty);
        DrawReferences(definitions, state);

        ImGui::Separator();
'''
new = r'''        DrawOutputStyles(definitions, state);
        DrawPaneSettings(state.draft, state.dirty);
        DrawReferences(definitions, state);

        ImGui::EndChild();
        ImGui::Separator();
'''
if ui.count(old) != 1:
    raise RuntimeError("indicator UI: editor child end mismatch")
ui = ui.replace(old, new)

write_text(ui_path, ui, ui_bom)

print("applied 0J index realtime and indicator manager acceptance fixes")
