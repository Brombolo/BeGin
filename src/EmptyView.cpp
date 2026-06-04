#include "EmptyView.h"
#include <StringView.h>
#include <LayoutBuilder.h>

EmptyView::EmptyView(const char* name)
    : BView(name, B_WILL_DRAW)
{
    // Set view color to match the system panel background
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    BStringView* statusView = new BStringView("status_label", "Nessun modulo caricato");
    statusView->SetAlignment(B_ALIGN_CENTER);

    // Increase font size or make it bold for better styling
    BFont font;
    statusView->GetFont(&font);
    font.SetSize(16.0);
    statusView->SetFont(&font, B_FONT_SIZE);

    // Layout: align the label in the center of the view
    BLayoutBuilder::Group<>(this, B_VERTICAL)
        .AddGlue()
        .Add(statusView)
        .AddGlue()
        .End();
}

EmptyView::~EmptyView()
{
}
