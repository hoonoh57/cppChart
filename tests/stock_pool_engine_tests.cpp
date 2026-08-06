#include "../core/stock_pool_engine.h"
#include "../app/stock_pool_evaluator.h"
#include "../app/stock_pool_fixture.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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
}

int main()
{
    using namespace trading::stock_pool;

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

    std::cout << "stock pool engine tests passed" << std::endl;
    return 0;
}
