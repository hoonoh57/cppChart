#include "../render/render_document.h"

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
}

int main()
{
    using namespace trading::render;

    RenderDocument document;
    document.workspaceId = "style-test";
    document.title = "Style test";

    Pane pane;
    pane.id = "lower";
    pane.title = "Lower";

    LegendEntry legend;
    legend.id = "legend.lower.one";
    legend.ownerId = "indicator.one";
    legend.label = "Styled";
    legend.width = 2.0f;
    legend.style = LineStyle::Dashed;
    pane.legends.push_back(legend);

    LineSeries line;
    line.id = "line.one";
    line.ownerId = "indicator.one";
    line.width = 2.0f;
    line.style = LineStyle::Dotted;
    pane.lines.push_back(line);

    ReferenceLine reference;
    reference.id = "reference.one";
    reference.ownerId = "indicator.one";
    reference.label = "Overbought";
    reference.value = 70.0;
    reference.width = 1.5f;
    reference.style = LineStyle::Dashed;
    pane.referenceLines.push_back(reference);

    document.panes.push_back(pane);
    std::string error;
    Check(ValidateRenderDocument(document, error),
          "styled generic render document must validate");

    document.panes.front().legends.front().width = 0.0f;
    Check(!ValidateRenderDocument(document, error),
          "invalid legend sample width must fail closed");

    std::puts("[PASS] render_style_tests");
    return 0;
}
