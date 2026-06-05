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
    MSG_FILE_EXIT = 'fext',
    MSG_LOAD_MODULE_PROMPT = 'ldmp',
    MSG_SELECT_MODULE = 'slmd',
    MSG_SHOW_MODULE_MANAGER = 'smmg'
};

class ModuleManagementView;

struct LoadedModule {
    image_id image;
    BaseModule* instance;
    BView* sidebarView; // The composite box containing the icon and label
    BView* view;
    int32 cardIndex;
    bool disabled;
    BString fileName;  // Original file name (e.g. TaskManager.so)
    BString signature; // Module unique signature
    node_ref nodeRef;  // Inode of the module file
};

class MainWindow : public BWindow {
public:
    MainWindow();
    virtual ~MainWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual void DispatchMessage(BMessage* message, BHandler* handler) override;
    virtual bool QuitRequested() override;
    virtual void Show() override;

    void LoadModule(const entry_ref& ref) { _LoadModule(ref); }
    void UnloadModuleByName(const char* name) { _UnloadModuleByName(name); }

private:
    void _InitInterface();
    void _InitModulesDirectory();
    void _LoadExistingModules();
    void _LoadModule(const entry_ref& ref);
    void _UnloadModuleByName(const char* name);
    void _UnloadModuleByNode(ino_t node, dev_t device);
    void _UnloadModule(LoadedModule& mod, bool dueToError, const char* reason);
    void _AddModuleToUI(LoadedModule& loaded);
    void _SelectModule(const BString& signature);
    BPath _GetModulesDirectory();
    
    // Fault Tolerance Helpers
    BaseModule* _FindModuleForHandler(BHandler* handler);
    void _DisableModule(BaseModule* module, const char* reason);
    void _WriteDeactivationLog(BaseModule* module, const char* reason);

    // Watchdog Thread Methods
    static int32 _WatchdogEntry(void* data);
    int32 _WatchdogLoop();

    // Signal handler registration
    static void _SignalHandler(int sig);

    BMenuBar*             fMenuBar;
    WarningBanner*        fWarningBanner;
    BGroupView*           fSidebarView;
    BCardView*            fCardView;
    EmptyView*            fEmptyView;
    BFilePanel*           fFilePanel;
    ModuleManagementView* fModuleManagementView;

    std::vector<LoadedModule> fModules;

    // Node Monitor
    node_ref fModulesDirNodeRef;

    // Watchdog variables
    thread_id                 fWatchdogThread;
    thread_id                 fLooperThread;
    std::atomic<BaseModule*>  fCurrentDispatchModule;
    std::atomic<bigtime_t>    fDispatchStartTime;
    std::atomic<bool>         fWatchdogRunning;

    // Safe jump buffers for non-local recovery during block interrupts
    sigjmp_buf                fJumpBuf;
    std::atomic<bool>         fCanJump;

    // Static instance pointer to resolve context inside POSIX signal handler
    static MainWindow*        sActiveWindow;
};

#endif // MAIN_WINDOW_H
