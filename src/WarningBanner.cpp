#include "WarningBanner.h"
#include <LayoutBuilder.h>

WarningBanner::WarningBanner(const char* name)
    : BView(name, B_WILL_DRAW)
{
    // High-contrast modern crimson red background
    SetViewColor(180, 20, 20);

    fTextView = new BStringView("warning_label", "");
    fTextView->SetAlignment(B_ALIGN_CENTER);
    fTextView->SetViewColor(180, 20, 20);
    fTextView->SetHighColor(255, 255, 255); // Crisp white text

    BFont font;
    fTextView->GetFont(&font);
    font.SetFace(B_BOLD_FACE);
    font.SetSize(11.0);
    fTextView->SetFont(&font, B_FONT_FACE | B_FONT_SIZE);

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(6, 4, 6, 4)
        .Add(fTextView)
        .End();

    Hide(); // Initially hidden
}

WarningBanner::~WarningBanner()
{
}

void WarningBanner::AddDeactivatedModule(const char* name, const char* reason)
{
    BString item;
    item << name << " [" << reason << "]";
    fDeactivatedNames.push_back(item);

    _UpdateText();
    
    if (IsHidden()) {
        Show();
    }
}

void WarningBanner::Clear()
{
    fDeactivatedNames.clear();
    _UpdateText();
    if (!IsHidden()) {
        Hide();
    }
}

void WarningBanner::_UpdateText()
{
    if (fDeactivatedNames.empty()) {
        fTextView->SetText("");
        return;
    }

    BString text = "DISATTIVATI PER SICUREZZA: ";
    for (size_t i = 0; i < fDeactivatedNames.size(); ++i) {
        if (i > 0) text << ", ";
        text << fDeactivatedNames[i];
    }
    fTextView->SetText(text.String());
}
