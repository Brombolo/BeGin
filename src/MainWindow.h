#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <Window.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <GroupView.h>
#include <CardView.h>
#include <FilePanel.h>
#include <Path.h>
#include <NodeMonitor.h>
#include <image.h>
#include <vector>
#include <atomic>
#include <setjmp.h>

#include "BaseModule.h"
#include "EmptyView.h"
#include "WarningBanner.h"

// Message constants
enum {
    MSG_FILE_EXIT           = 'fext',
    MSG_SELECT_MODULE       = 'slmd',
    MSG_SHOW_SETTINGS       = 'sset',  // Open the settings overlay
    MSG_HIDE_SETTINGS       = 'hdst',  // Close settings and return to main view
};

class ModuleManagementView;

// Represents a fully loaded and active add-on module
struct LoadedModule {
    image_id    image;
    BaseModule* instance;
    BView*      sidebarView; // Composite box (icon + label) in the sidebar
    BView*      view;        // The module's content view inside the card layout
    int32       cardIndex;   // Index inside fModuleCardView
    bool        disabled;
    BString     fileName;    // File name on disk (e.g. "TaskManager.so")
    BString     signature;   // Unique module signature string
    node_ref    nodeRef;     // Filesystem inode reference for live tracking
};

class MainWindow : public BWindow {
public:
    MainWindow();
    virtual ~MainWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual void DispatchMessage(BMessage* message, BHandler* handler) override;
    virtual bool QuitRequested() override;
    virtual void Show() override;

    // Public wrappers used by ModuleManagementView
    void LoadModule(const entry_ref& ref)    { _LoadModule(ref); }
    void UnloadModuleByName(const char* name) { _UnloadModuleByName(name); }

private:
    // Initialisation helpers
    void  _InitInterface();
    void  _InitModulesDirectory();
    void  _LoadExistingModules();
    BPath _GetModulesDirectory();

    // Module loading / unloading
    void  _LoadModule(const entry_ref& ref);
    void  _LoadModuleDirect(const char* path, const char* fileName);
    void  _UnloadModuleByName(const char* name);
    void  _UnloadModuleByNode(ino_t node, dev_t device);
    void  _UnloadModule(LoadedModule& mod, bool dueToError, const char* reason);
    void  _AddModuleToUI(LoadedModule& loaded);
    void  _SelectModule(const BString& signature);
    void  _SelectFirstActiveModule();

    // Fault tolerance helpers
    BaseModule* _FindModuleForHandler(BHandler* handler);
    void  _DisableModule(BaseModule* module, const char* reason);
    void  _WriteDeactivationLog(BaseModule* module, const char* reason);
    void  _RenameModuleFileDisabled(const BString& fileName);

    // Watchdog thread
    static int32 _WatchdogEntry(void* data);
    int32  _WatchdogLoop();

    // POSIX signal handler
    static void _SignalHandler(int sig);

    // ── UI widgets ──────────────────────────────────────────────────────────
    BMenuBar*             fMenuBar;        // Main menu bar (File | ⚙)
    WarningBanner*        fWarningBanner;

    // Outer card: Card 0 = normal app view, Card 1 = settings overlay
    BCardView*            fMainCardView;

    // Normal app view elements
    BGroupView*           fSidebarView;    // Left sidebar with module buttons
    BCardView*            fModuleCardView; // Right content area (one card per module)
    EmptyView*            fEmptyView;      // Placeholder shown when no module is selected

    // Settings overlay elements
    ModuleManagementView* fModuleManagementView;

    // ── State ───────────────────────────────────────────────────────────────
    std::vector<LoadedModule> fModules;

    // Node Monitor – watches the add-ons directory for live file changes
    node_ref fModulesDirNodeRef;

    // Watchdog thread
    thread_id               fWatchdogThread;
    thread_id               fLooperThread;
    std::atomic<BaseModule*> fCurrentDispatchModule;
    std::atomic<bigtime_t>   fDispatchStartTime;
    std::atomic<bool>        fWatchdogRunning;

    // Non-local jump buffer for safe watchdog recovery
    sigjmp_buf              fJumpBuf;
    std::atomic<bool>       fCanJump;

    // Static back-pointer used inside the POSIX signal handler
    static MainWindow* sActiveWindow;
};

#endif // MAIN_WINDOW_H
