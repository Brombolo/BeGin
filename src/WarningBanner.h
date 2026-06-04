#ifndef WARNING_BANNER_H
#define WARNING_BANNER_H

#include <View.h>
#include <StringView.h>
#include <String.h>
#include <vector>

class WarningBanner : public BView {
public:
    WarningBanner(const char* name);
    virtual ~WarningBanner();

    void AddDeactivatedModule(const char* name, const char* reason);
    void Clear();
    bool HasDeactivations() const { return !fDeactivatedNames.empty(); }

private:
    void _UpdateText();

    BStringView*         fTextView;
    std::vector<BString> fDeactivatedNames;
};

#endif // WARNING_BANNER_H
