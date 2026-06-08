#include "WarningBanner.h"
#include <LayoutBuilder.h>

WarningBanner::WarningBanner(const char* name)
    : BView(name, B_WILL_DRAW)
{
    // High-contrast modern crimson red background
    SetViewColor(180, 20, 20);

    // Create BTextView for multi-line wrapped text
    fTextView = new BTextView("warning_text");
    fTextView->MakeEditable(false);
    fTextView->MakeSelectable(false);
    fTextView->SetStylable(true);
    fTextView->SetAlignment(B_ALIGN_CENTER);
    
    // Set background and text colors
    fTextView->SetViewColor(180, 20, 20);
    fTextView->SetHighColor(255, 255, 255);
    
    // Set font to Bold
    BFont font;
    fTextView->GetFont(&font);
    font.SetFace(B_BOLD_FACE);
    font.SetSize(11.0);
    fTextView->SetFontAndColor(&font, B_FONT_FACE | B_FONT_SIZE);

    fTextView->SetExplicitMinSize(BSize(10, 16));
    fTextView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 50));

    // Create a small close button
    fCloseBtn = new BButton("x", new BMessage(MSG_CLOSE_BANNER));
    fCloseBtn->SetExplicitMinSize(BSize(20, 20));
    fCloseBtn->SetExplicitMaxSize(BSize(20, 20));

    SetExplicitMinSize(BSize(10, 28));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60));

    // Lay out horizontally: text takes all space, close button is packed on the right
    BLayoutBuilder::Group<>(this, B_HORIZONTAL, 5)
        .SetInsets(6, 4, 6, 4)
        .Add(fTextView, 1.0f)
        .Add(fCloseBtn, 0.0f)
        .End();

    Hide(); // Initially hidden
}

WarningBanner::~WarningBanner()
{
}

void WarningBanner::AttachedToWindow()
{
    BView::AttachedToWindow();
    fCloseBtn->SetTarget(this);
}

void WarningBanner::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_CLOSE_BANNER:
            Hide();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}

void WarningBanner::AddDeactivatedModule(const char* name, const char* reason)
{
    // Classify the deactivation reason into a short English label
    BString shortReason = "Error";
    BString reasonStr(reason);
    if (reasonStr.FindFirst("Watchdog") >= 0 || reasonStr.FindFirst("hang") >= 0) {
        shortReason = "Hang";
    } else if (reasonStr.FindFirst("Exception") >= 0 || reasonStr.FindFirst("exception") >= 0) {
        shortReason = "Exception";
    }

    BString item;
    item << name << " (" << shortReason << ")";
    fDeactivatedNames.push_back(item);

    _UpdateText();

    if (IsHidden())
        Show();
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

    BString text = "DEACTIVATED: ";
    for (size_t i = 0; i < fDeactivatedNames.size(); ++i) {
        if (i > 0) text << ", ";
        text << fDeactivatedNames[i];
    }
    fTextView->SetText(text.String());

    // Reset text color after SetText (required by BTextView)
    rgb_color white = {255, 255, 255, 255};
    fTextView->SetFontAndColor(nullptr, 0, &white);
}
