// ModuleManagementView.h – Settings panel for managing add-on modules
#ifndef MODULE_MANAGEMENT_VIEW_H
#define MODULE_MANAGEMENT_VIEW_H

#include <View.h>
#include <ListView.h>
#include <ScrollView.h>
#include <Button.h>
#include <StringView.h>
#include <Entry.h>
#include <Path.h>
#include <FilePanel.h>
#include <String.h>
#include <vector>

class MainWindow;

// Describes one file entry found in the add-ons directory
struct ModuleFileItem {
    entry_ref ref;
    BString   fileName; // e.g. "TaskManager.so" or "TaskManager.so.disabled"
    bool      isActive; // true  → ends with ".so"
                        // false → ends with ".so.disabled"
    BString   version;  // Version string read from the module binary
};

class ModuleManagementView : public BView {
public:
    explicit ModuleManagementView(MainWindow* parent);
    virtual ~ModuleManagementView();

    virtual void AttachedToWindow() override;
    virtual void MessageReceived(BMessage* message) override;

    // Re-scans the add-ons directory and refreshes the list
    void ScanModules();

private:
    void    _InitInterface();
    void    _UpdateDetails();
    void    _ToggleModule();   // Enable / disable (rename .so ↔ .so.disabled)
    void    _ReloadModule();   // Unload + reload the selected active module
    void    _DeleteModule();   // Permanently remove the module file
    void    _LoadModule();     // Open a BFilePanel to load a new .so

    // Temporarily loads an add-on to read its version string
    BString _ReadModuleVersion(const entry_ref& ref, bool isActive);

    MainWindow*  fParent;
    BListView*   fListView;
    BScrollView* fScrollView;
    BButton*     fLoadBtn;     // "Load Module…" button inside the panel
    BFilePanel*  fFilePanel;   // File-open panel (lazy-initialised)

    // Details / action panel
    BView*       fDetailsContainer;
    BStringView* fNameLabel;
    BStringView* fVersionLabel;
    BStringView* fFileLabel;
    BStringView* fStatusLabel;
    BButton*     fToggleBtn;
    BButton*     fReloadBtn;
    BButton*     fDeleteBtn;

    std::vector<ModuleFileItem> fModuleFiles;
};

#endif // MODULE_MANAGEMENT_VIEW_H
