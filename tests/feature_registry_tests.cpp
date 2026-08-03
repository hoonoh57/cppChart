#include "../app/feature_registry.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }

    void Check(bool condition, const char* message)
    {
        if (!condition) Fail(message);
    }

    void TestRegistrationAndDependencies()
    {
        trading::app::FeatureRegistry registry;
        std::string error;

        Check(registry.Register(
                  "market-data",
                  "Market data",
                  trading::app::FeatureLevel::Visible,
                  {},
                  error),
              "market-data registration failed");

        Check(registry.Register(
                  "chart-workspace",
                  "Chart workspace",
                  trading::app::FeatureLevel::Visible,
                  { "market-data" },
                  error),
              "chart-workspace registration failed");

        Check(!registry.SetLevel(
                  "market-data",
                  trading::app::FeatureLevel::Off,
                  error),
              "enabled dependent must prevent disabling market-data");
        Check(error.find("chart-workspace") != std::string::npos,
              "dependency rejection must identify dependent feature");

        Check(registry.SetLevel(
                  "chart-workspace",
                  trading::app::FeatureLevel::Off,
                  error),
              "chart-workspace Off transition failed");
        Check(registry.SetLevel(
                  "market-data",
                  trading::app::FeatureLevel::Off,
                  error),
              "market-data Off transition failed");

        Check(!registry.SetLevel(
                  "chart-workspace",
                  trading::app::FeatureLevel::Visible,
                  error),
              "feature must not activate while dependency is Off");
        Check(error.find("market-data") != std::string::npos,
              "dependency error must identify market-data");
    }

    void TestMetricsAndHealth()
    {
        trading::app::FeatureRegistry registry;
        std::string error;
        Check(registry.Register(
                  "trading",
                  "Trading",
                  trading::app::FeatureLevel::Active,
                  {},
                  error),
              "trading registration failed");

        Check(registry.SetHealth(
                  "trading",
                  true,
                  {},
                  error),
              "feature health update failed");

        Check(registry.RecordWork(
                  "trading",
                  42,
                  3,
                  4096,
                  7,
                  2,
                  5,
                  1,
                  error),
              "feature work record failed");

        Check(registry.RecordWork(
                  "trading",
                  84,
                  1,
                  8192,
                  8,
                  3,
                  2,
                  0,
                  error),
              "second feature work record failed");

        trading::app::FeatureSnapshot snapshot;
        Check(registry.Get("trading", snapshot),
              "feature snapshot missing");
        Check(snapshot.ready, "feature readiness mismatch");
        Check(snapshot.metrics.eventCount == 2,
              "feature event count mismatch");
        Check(snapshot.metrics.lastProcessingMicros == 84,
              "feature latest duration mismatch");
        Check(snapshot.metrics.maxProcessingMicros == 84,
              "feature maximum duration mismatch");
        Check(snapshot.metrics.mergedEventCount == 7,
              "feature merged-event count mismatch");
        Check(snapshot.metrics.droppedEventCount == 1,
              "feature dropped-event count mismatch");
        Check(snapshot.metrics.retainedBytes == 8192,
              "feature retained-byte count mismatch");
    }
}

int main()
{
    TestRegistrationAndDependencies();
    TestMetricsAndHealth();
    std::puts("[PASS] feature_registry_tests");
    return 0;
}
