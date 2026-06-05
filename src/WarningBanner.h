#ifndef WARNING_BANNER_H
#define WARNING_BANNER_H

#include <View.h>
#include <TextView.h>
#include <Button.h>
#include <String.h>
#include <vector>

enum {
    MSG_CLOSE_BANNER = 'clbn'
};

class WarningBanner : public BView {
public:
    WarningBanner(const char* name);
    virtual ~WarningBanner();

    virtual void MessageReceived(BMessage* message) override;
    virtual void AttachedToWindow() override;

    void AddDeactivatedModule(const char* name, const char* reason);
    void Clear();
    bool HasDeactivations() const { return !fDeactivatedNames.empty(); }

private:
    void _UpdateText();

    BTextView*           fTextView;
    BButton*             fCloseBtn;
    std::vector<BString> fDeactivatedNames;
};

#endif // WARNING_BANNER_H
