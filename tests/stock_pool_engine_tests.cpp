#include "../core/stock_pool_engine.h"
#include "../app/stock_pool_evaluator.h"
#include "../app/stock_pool_fixture.h"
#include "../app/stock_pool_1516_import.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }

    const trading::stock_pool::RankRow* FindRow(
        const trading::stock_pool::RankingSnapshot& snapshot,
        const std::string& code)
    {
        for (const auto& row : snapshot.rows) {
            if (row.code == code) return &row;
        }
        return nullptr;
    }

    void Verify1516ClipboardImport()
    {
        using namespace trading::stock_pool::import1516;
        const std::string clipboard =
            "\t종목명\t기간 수익률\t\t\t기간 내 최고수익률\t검색시점 거래량\t기타\n"
            "\t\t1분간\t3분간\t7시간\n"
            "\t아로마티카\t\"+6.81%\"\t\"+3.61%\"\t\"+24.31%\"\t\"+24.31%\"\t\"53,841\"\t\"14.86\"\n"
            "\t져스텍\t\"0%\"\t\"+1.10%\"\t\"+13.46%\"\t\"+21.81%\"\t\"82,193\"\t\"16.24\"\n"
            "\t아로마티카\t\"-1.00%\"\t\"0%\"\t\"+1.00%\"\t\"+2.00%\"\t\"1,000\"\t\"-0.50\"\n";

        ParseResult parsed = ParseClipboardText(clipboard);
        Require(parsed.rows.size() == 3U, "1516 parsed row count");
        Require(
            std::abs(parsed.rows[0].return1mPercent - 6.81) < 1.0e-9,
            "1516 percentage parsing");
        Require(
            parsed.rows[0].captureVolume == 53841,
            "1516 volume parsing");
        Require(
            std::abs(parsed.rows[1].return1mPercent) < 1.0e-9,
            "1516 zero percentage parsing");

        const std::vector<SymbolMasterEntry> master = {
            {"123450", "아로마티카", "KOSDAQ"},
            {"005930", "삼성전자", "KOSPI"}};
        ResolveExactSymbolNames(parsed.rows, master);
        Require(
            parsed.rows[0].status == ResolutionStatus::Resolved &&
                parsed.rows[0].code == "123450",
            "1516 exact symbol resolution");
        Require(
            parsed.rows[1].status == ResolutionStatus::MissingSymbol,
            "1516 missing symbol rejection");
        Require(
            parsed.rows[2].status == ResolutionStatus::DuplicateSymbol,
            "1516 duplicate symbol rejection");
    }
}

int main()
{
    using namespace trading::stock_pool;

    Verify1516ClipboardImport();

    const ScoringProfile profile;
    const auto members = fixture::BuildDeterministicFixture();
    Require(members.size() == 12U, "fixture member count");

    RankingEngine insufficientEngine;
    const RankingSnapshot insufficient =
        insufficientEngine.Evaluate(members, 2U, 2, profile);
    Require(insufficient.noTrade, "insufficient history must fail closed");

    RankingEngine causalEngineA;
    const RankingSnapshot beforeMutation =
        causalEngineA.Evaluate(members, 20U, 2, profile);
    auto mutated = members;
    mutated.front().bars[80].close *= 100.0;
    mutated.front().bars[80].high *= 100.0;
    RankingEngine causalEngineB;
    const RankingSnapshot afterMutation =
        causalEngineB.Evaluate(mutated, 20U, 2, profile);
    Require(
        beforeMutation.rows.size() == afterMutation.rows.size(),
        "causal snapshot row count");
    for (std::size_t index = 0U;
         index < beforeMutation.rows.size();
         ++index)
    {
        const RankRow& left = beforeMutation.rows[index];
        const RankRow& right = afterMutation.rows[index];
        Require(left.code == right.code, "future mutation changed rank order");
        Require(
            std::abs(left.strength - right.strength) < 1.0e-9,
            "future mutation changed past strength");
    }

    RankingEngine replayEngine;
    RankingSnapshot snapshot;
    for (std::size_t index = 5U; index <= 25U; ++index) {
        snapshot = replayEngine.Evaluate(members, index, 2, profile);
    }
    const RankRow* earlyLeader = FindRow(snapshot, "F0001");
    Require(earlyLeader != nullptr, "early leader row missing");
    Require(earlyLeader->rank <= 2, "early leader must rank in Top-2");
    Require(
        earlyLeader->state == LeaderState::ConfirmedLeader ||
        earlyLeader->state == LeaderState::PersistentLeader,
        "early leader must be confirmed by persistence");

    const BacktestResult backtest = RunTopMBacktest(members, 2, profile);
    Require(!backtest.snapshots.empty(), "backtest snapshots missing");
    Require(!backtest.trades.empty(), "Top-M backtest produced no trades");

    const evaluation::EvaluationReport report =
        evaluation::EvaluateWinnerCapture(members, backtest, 3);
    Require(report.winnerCount == 3, "winner evaluation count");
    Require(
        report.capturedWinnerCount >= 1,
        "causal ranking captured no fixture winner");

    std::cout << "stock pool engine and 1516 import tests passed" << std::endl;
    return 0;
}
