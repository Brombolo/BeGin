#ifndef MODULE_MANAGEMENT_VIEW_H
#define MODULE_MANAGEMENT_VIEW_H

#include <View.h>
#include <ListView.h>
#include <ScrollView.h>
#include <Button.h>
#include <StringView.h>
#include <Entry.h>
#include <Path.h>
#include <vector>

class MainWindow;

struct ModuleFileItem {
    entry_ref ref;
    BString   fileName;
    bool      isActive; // true if ends with .so, false if .so.disabled
};

class ModuleManagementView : public BView {
public:
    ModuleManagementView(MainWindow* parent);
    virtual ~ModuleManagementView();

    virtual void AttachedToWindow() override;
    virtual void MessageReceived(BMessage* message) override;

    void ScanModules();

private:
    void _InitInterface();
    void _UpdateDetails();
    void _ToggleModule();
    void _ReloadModule();
    void _DeleteModule();

    MainWindow*               fParent;
    BListView*                fListView;
    BScrollView*              fScrollView;
    
    // Details panel views
    BView*                    fDetailsContainer;
    BStringView*              fNameLabel;
    BStringView*              fFileLabel;
    BStringView*              fStatusLabel;
    BButton*                  fToggleBtn;
    BButton*                  fReloadBtn;
    BButton*                  fDeleteBtn;

    std::vector<ModuleFileItem> fModuleFiles;
};

#endif // MODULE_MANAGEMENT_VIEW_H
