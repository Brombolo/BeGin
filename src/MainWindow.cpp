// MainWindow.cpp – Host application main window implementation
#include "MainWindow.h"
#include "ModuleManagementView.h"

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <CardLayout.h>
#include <LayoutBuilder.h>
#include <SpaceLayoutItem.h>
#include <TabView.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <String.h>
#include <StringView.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>

// ── Static member definition ─────────────────────────────────────────────────
MainWindow* MainWindow::sActiveWindow = nullptr;

// ── Constructor ──────────────────────────────────────────────────────────[...]
MainWindow::MainWindow()
    : BWindow(BRect(100, 100, 860, 620), "BeGin", B_TITLED_WINDOW,
               B_AUTO_UPDATE_SIZE_LIMITS),
      fMenuBar(nullptr),
      fWarningBanner(nullptr),
      fMainCardView(nullptr),
      fSidebarView(nullptr),
      fModuleCardView(nullptr),
      fEmptyView(nullptr),
      fModuleManagementView(nullptr),
      fWatchdogThread(-1),
      fLooperThread(-1),
      fCurrentDispatchModule(nullptr),
      fDispatchStartTime(0),
      fWatchdogRunning(true),
      fCanJump(false)
{
    sActiveWindow = this;

    // Register the POSIX signal handler used by the watchdog thread
    struct sigaction sa;
    sa.sa_handler = _SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, nullptr);

    // Ensure the add-ons directory exists before building the UI
    _InitModulesDirectory();
    _InitInterface();

    // Start the watchdog thread
    fWatchdogThread = spawn_thread(_WatchdogEntry, "begin_watchdog",
                                   B_NORMAL_PRIORITY, this);
    if (fWatchdogThread >= 0)
        resume_thread(fWatchdogThread);

    // Load any modules already sitting in the add-ons directory
    _LoadExistingModules();
    _SelectFirstActiveModule();
}

// ── Destructor ───────────────────────────────────────────────────────────[...]
MainWindow::~MainWindow()
{
    // Stop the node monitor before anything else
    stop_watching(this);

    // Signal and join the watchdog thread
    fWatchdogRunning.store(false);
    if (fWatchdogThread >= 0) {
        status_t exitVal;
        wait_for_thread(fWatchdogThread, &exitVal);
    }

    if (sActiveWindow == this)
        sActiveWindow = nullptr;
}

// ── Show ─────────────────────────────────────────────────────────────[...]
void MainWindow::Show()
{
    BWindow::Show();

    // Record the looper thread ID now that the window looper is running
    fLooperThread = Thread();

    // Start watching the add-ons directory for live file-system changes
    BPath path = _GetModulesDirectory();
    BDirectory dir(path.Path());
    if (dir.InitCheck() == B_OK) {
        dir.GetNodeRef(&fModulesDirNodeRef);
        watch_node(&fModulesDirNodeRef, B_WATCH_DIRECTORY, this);
    }
}

// ── MessageReceived ─────────────────────────────────────────────────────────[...]
void MainWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {

        case MSG_FILE_EXIT:
            PostMessage(B_QUIT_REQUESTED);
            break;

        // ── File-panel result from the old direct load path ──────────────
        case B_REFS_RECEIVED:
        {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK)
                _LoadModule(ref);
            break;
        }

        // ── Module navigation ─────────────────────────────────────────────
        case MSG_SELECT_MODULE:
        {
            BString signature;
            if (message->FindString("signature", &signature) == B_OK)
                _SelectModule(signature);
            break;
        }

        // ── Settings overlay: show ────────────────────────────────────────
        case MSG_SHOW_SETTINGS:
        {
            if (fMainCardView->CardLayout() != nullptr) {
                fMainCardView->CardLayout()->SetVisibleItem((int32)1);
                if (fModuleManagementView != nullptr)
                    fModuleManagementView->ScanModules();
            }
            break;
        }

        // ── Settings overlay: hide / confirm ──────────────────────────────
        case MSG_HIDE_SETTINGS:
        {
            if (fMainCardView->CardLayout() != nullptr) {
                fMainCardView->CardLayout()->SetVisibleItem((int32)0);
                _SelectFirstActiveModule();
            }
            break;
        }

        // ── Node Monitor: live add-ons directory changes ───────────────────
        case B_NODE_MONITOR:
        {
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) != B_OK)
                break;

            if (opcode == B_ENTRY_CREATED) {
                const char* name;
                dev_t device;
                ino_t directory;
                if (message->FindString("name", &name) == B_OK &&
                    message->FindInt32("device", &device) == B_OK &&
                    message->FindInt64("directory", &directory) == B_OK) {

                    BString filename(name);
                    if (filename.EndsWith(".so")) {
                        entry_ref ref(device, directory, name);
                        _LoadModule(ref);
                    }
                }

            } else if (opcode == B_ENTRY_REMOVED) {
                // B_ENTRY_REMOVED does NOT carry the file name — use the inode
                dev_t device;
                ino_t node;
                if (message->FindInt32("device", &device) == B_OK &&
                    message->FindInt64("node", &node) == B_OK)
                    _UnloadModuleByNode(node, device);

            } else if (opcode == B_ENTRY_MOVED) {
                dev_t device;
                ino_t fromDir, toDir, node;
                const char* name;
                if (message->FindInt32("device", &device) == B_OK &&
                    message->FindInt64("from_directory", &fromDir) == B_OK &&
                    message->FindInt64("to_directory",   &toDir)   == B_OK &&
                    message->FindInt64("node",           &node)    == B_OK &&
                    message->FindString("name",          &name)    == B_OK) {

                    bool fromWatched = (device == fModulesDirNodeRef.device &&
                                        fromDir == fModulesDirNodeRef.node);
                    bool toWatched   = (device == fModulesDirNodeRef.device &&
                                        toDir   == fModulesDirNodeRef.node);
                    BString filename(name);

                    if (fromWatched && !toWatched) {
                        // File moved OUT of add-ons directory
                        _UnloadModuleByNode(node, device);

                    } else if (!fromWatched && toWatched) {
                        // File moved INTO add-ons directory
                        if (filename.EndsWith(".so")) {
                            entry_ref ref(device, toDir, name);
                            _LoadModule(ref);
                        }

                    } else if (fromWatched && toWatched) {
                        // Renamed inside add-ons (e.g. .so ↔ .so.disabled)
                        if (filename.EndsWith(".so")) {
                            entry_ref ref(device, toDir, name);
                            _LoadModule(ref);
                        } else {
                            _UnloadModuleByNode(node, device);
                        }
                    }
                }
            }

            // Keep the settings panel list in sync
            if (fModuleManagementView != nullptr)
                fModuleManagementView->ScanModules();

            break;
        }

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

// ── QuitRequested ─────────────────────────────────────────────────────────[...]
bool MainWindow::QuitRequested()
{
    // Stop the node monitor first to avoid stray messages during shutdown
    stop_watching(this);

    // Cleanly unload every active module
    for (auto& loaded : fModules)
        _UnloadModule(loaded, false, nullptr);
    fModules.clear();

    // Ask BApplication to terminate as well
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

// ── _InitInterface ──────────────────────────────────────────────────────────[...]
void MainWindow::_InitInterface()
{
    // ── Menu bar ──────────────────────────────────────────────────────────[...]
    fMenuBar = new BMenuBar("menubar");

    BMenu* fileMenu = new BMenu("File");
    fileMenu->AddItem(new BMenuItem("Quit", new BMessage(MSG_FILE_EXIT), 'Q'));
    fMenuBar->AddItem(fileMenu);

    // Spacer so the gear sits flush against the right edge
    fMenuBar->AddItem(new BMenuBar("spacer_menu"));

    // Gear (⚙) settings button — opens the settings overlay
    BMenu* gearMenu = new BMenu("\xe2\x9a\x99"); // UTF-8 for ⚙
    gearMenu->AddItem(new BMenuItem("Settings...",
                                    new BMessage(MSG_SHOW_SETTINGS), ','));
    fMenuBar->AddItem(gearMenu);

    // ── Warning banner (hidden until a module is deactivated) ─────────────────
    fWarningBanner = new WarningBanner("warning_banner");

    // ── Sidebar (left column, fixed 90 px wide) ───────────────────────────────
    fSidebarView = new BGroupView(B_VERTICAL, 10);
    fSidebarView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    fSidebarView->SetExplicitMinSize(BSize(90, 0));
    fSidebarView->SetExplicitMaxSize(BSize(90, B_SIZE_UNLIMITED));
    // Glue pushes module buttons toward the top
    fSidebarView->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());

    // ── Module content area (BCardView, one card per module) ──────────────────
    fModuleCardView = new BCardView();
    fEmptyView = new EmptyView("empty_view");
    fModuleCardView->AddChild(fEmptyView); // Card 0: no module selected

    // ── Normal app view: sidebar + module card ────────────────────────────────
    BView* normalView = new BView("normal_view", 0);
    BLayoutBuilder::Group<>(normalView, B_HORIZONTAL, 0)
        .Add(fSidebarView,    1.0f)
        .Add(fModuleCardView, 4.0f)
        .End();

    // ── Settings overlay: tab view + confirm button ───────────────────────────
    fModuleManagementView = new ModuleManagementView(this);

    BTabView* tabView = new BTabView("settings_tabs", B_WIDTH_FROM_LABEL);
    tabView->AddTab(fModuleManagementView);
    tabView->TabAt(0)->SetLabel("Modules");

    BButton* confirmBtn = new BButton("confirm_btn", "Done",
                                      new BMessage(MSG_HIDE_SETTINGS));
    confirmBtn->SetExplicitMaxSize(BSize(120, B_SIZE_UNSET));

    BView* settingsView = new BView("settings_view", 0);
    BLayoutBuilder::Group<>(settingsView, B_VERTICAL, 8)
        .SetInsets(8, 8, 8, 8)
        .Add(tabView, 1.0f)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(confirmBtn)
        .End()
        .End();

    // ── Outer card: Card 0 = normal, Card 1 = settings ───────────────────────
    fMainCardView = new BCardView();
    fMainCardView->AddChild(normalView);   // Card 0
    fMainCardView->AddChild(settingsView); // Card 1

    // ── Window layout ─────────────────────────────────────────────────────────[...]
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .Add(fWarningBanner)
        .Add(fMainCardView, 1.0f)
        .End();
}

// ── _GetModulesDirectory ──────────────────────────────────────────────────────
BPath MainWindow::_GetModulesDirectory()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
        path.Append("BeGin/add-ons");
    return path;
}

// ── _InitModulesDirectory ─────────────────────────────────────────────────────
void MainWindow::_InitModulesDirectory()
{
    BPath path = _GetModulesDirectory();
    create_directory(path.Path(), 0755);
}

// ── _LoadExistingModules ──────────────────────────────────────────────────────
void MainWindow::_LoadExistingModules()
{
    BPath path = _GetModulesDirectory();
    BDirectory dir(path.Path());
    if (dir.InitCheck() != B_OK)
        return;

    entry_ref ref;
    while (dir.GetNextRef(&ref) == B_OK) {
        BString filename(ref.name);
        if (filename.EndsWith(".so"))
            _LoadModule(ref);
    }
}

// ── _LoadModule ───────────────────────────────────────────────────────────[...]
// Handles the full loading pipeline, including version-conflict detection when
// a file from an external location is being installed into the add-ons folder.
void MainWindow::_LoadModule(const entry_ref& ref)
{
    BEntry entry(&ref, true);
    BPath sourcePath;
    if (entry.GetPath(&sourcePath) != B_OK) {
        BAlert* alert = new BAlert("Load Error",
            "Could not resolve the module file path.", "OK",
            nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    BPath destDir  = _GetModulesDirectory();
    BPath destPath = destDir;
    destPath.Append(ref.name);

    BString fileName(ref.name);
    bool isExternalFile = (strcmp(sourcePath.Path(), destPath.Path()) != 0);

    // ── Version-conflict check when the file is coming from outside ───────────
    if (isExternalFile && BEntry(destPath.Path()).Exists()) {
        // Peek at the incoming module's signature & version
        image_id probeImg = load_add_on(sourcePath.Path());
        BString  incomingVersion = "unknown";
        BString  incomingSig;
        if (probeImg >= 0) {
            instantiate_module_func fn = nullptr;
            if (get_image_symbol(probeImg, "instantiate_module",
                                 B_SYMBOL_TYPE_TEXT, (void**)&fn) == B_OK && fn) {
                BaseModule* probe = fn();
                if (probe) {
                    incomingSig     = probe->Signature();
                    incomingVersion = probe->Version();
                    delete probe;
                }
            }
            unload_add_on(probeImg);
        }

        // Peek at the installed version
        image_id existImg = load_add_on(destPath.Path());
        BString  existingVersion = "unknown";
        if (existImg >= 0) {
            instantiate_module_func fn = nullptr;
            if (get_image_symbol(existImg, "instantiate_module",
                                 B_SYMBOL_TYPE_TEXT, (void**)&fn) == B_OK && fn) {
                BaseModule* probe = fn();
                if (probe) {
                    existingVersion = probe->Version();
                    delete probe;
                }
            }
            unload_add_on(existImg);
        }

        // Ask the user whether to overwrite
        BString msg;
        msg << "A module with this name is already installed.\n\n"
            << "  Installed version:  " << existingVersion << "\n"
            << "  New version:        " << incomingVersion << "\n\n"
            << "Do you want to replace the installed module?";

        BAlert* confirm = new BAlert("Module Already Installed", msg.String(),
                                     "Cancel", "Replace",
                                     nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        if (confirm->Go() != 1)
            return; // User chose Cancel

        // Unload the running instance (if any) before overwriting the file
        for (auto it = fModules.begin(); it != fModules.end(); ) {
            if (it->fileName == fileName) {
                _UnloadModule(*it, false, nullptr);
                it = fModules.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ── Copy file to add-ons directory if coming from outside ────────────────
    if (isExternalFile) {
        try {
            std::filesystem::copy_file(sourcePath.Path(), destPath.Path(),
                std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            BString errText = "Could not copy the module to the add-ons folder:\n";
            errText << e.what();
            BAlert* alert = new BAlert("Copy Error", errText.String(), "OK",
                nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
            alert->Go();
            return;
        }
    }

    _LoadModuleDirect(destPath.Path(), fileName.String());
}

// ── _LoadModuleDirect ─────────────────────────────────────────────────────────[...]
// Loads an add-on from a resolved path and registers it with the UI.
void MainWindow::_LoadModuleDirect(const char* path, const char* fileName)
{
    // Load the shared library into memory
    image_id image = load_add_on(path);
    if (image < 0) {
        BAlert* alert = new BAlert("Load Error",
            "Could not load the selected .so module.", "OK",
            nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Locate the factory function
    instantiate_module_func instantiateFunc = nullptr;
    if (get_image_symbol(image, "instantiate_module", B_SYMBOL_TYPE_TEXT,
                          (void**)&instantiateFunc) != B_OK || !instantiateFunc) {
        unload_add_on(image);
        BAlert* alert = new BAlert("Load Error",
            "The module does not export the required 'instantiate_module' symbol.",
            "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Instantiate the module object
    BaseModule* instance = instantiateFunc();
    if (!instance) {
        unload_add_on(image);
        BAlert* alert = new BAlert("Load Error",
            "The module factory returned a null instance.", "OK",
            nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Retrieve the inode reference of the file for future node-monitor matching
    BEntry destEntry(path, true);
    node_ref nodeRef;
    if (destEntry.GetNodeRef(&nodeRef) != B_OK)
        memset(&nodeRef, 0, sizeof(nodeRef));

    BString fileNameStr(fileName);
    BString signature(instance->Signature());

    // Guard against loading a duplicate of an already-active module
    for (auto it = fModules.begin(); it != fModules.end(); ) {
        bool sameNode = (nodeRef.node != 0 &&
                         it->nodeRef.node   == nodeRef.node &&
                         it->nodeRef.device == nodeRef.device);

        if (it->fileName == fileNameStr || it->signature == signature || sameNode) {
            if (it->disabled) {
                // Replace the stale disabled entry
                it = fModules.erase(it);
            } else {
                // An active module with the same identity is already loaded
                delete instance;
                unload_add_on(image);
                return; // Silently ignore — caller already confirmed or handled it
            }
        } else {
            ++it;
        }
    }

    // Register the module with the window looper so it can receive messages
    AddHandler(instance);

    LoadedModule loaded;
    loaded.image     = image;
    loaded.instance  = instance;
    loaded.sidebarView = nullptr;
    loaded.view      = nullptr;
    loaded.cardIndex = -1;
    loaded.disabled  = false;
    loaded.fileName  = fileNameStr;
    loaded.signature = signature;
    loaded.nodeRef   = nodeRef;

    _AddModuleToUI(loaded);
    fModules.push_back(loaded);

    // Auto-select this module if it is the only one active
    int32 activeCount = 0;
    LoadedModule* onlyActive = nullptr;
    for (auto& mod : fModules) {
        if (!mod.disabled && mod.instance) {
            activeCount++;
            onlyActive = &mod;
        }
    }
    if (activeCount == 1 && onlyActive)
        _SelectModule(onlyActive->signature);
}

// ── _UnloadModuleByName ───────────────────────────────────────────────────────
void MainWindow::_UnloadModuleByName(const char* name)
{
    BString fileName(name);
    for (auto it = fModules.begin(); it != fModules.end(); ++it) {
        if (it->fileName == fileName) {
            _UnloadModule(*it, false, nullptr);
            fModules.erase(it);
            break;
        }
    }
}

// ── _UnloadModuleByNode ───────────────────────────────────────────────────────
void MainWindow::_UnloadModuleByNode(ino_t node, dev_t device)
{
    for (auto it = fModules.begin(); it != fModules.end(); ++it) {
        if (it->nodeRef.node == node && it->nodeRef.device == device) {
            _UnloadModule(*it, false, nullptr);
            fModules.erase(it);
            break;
        }
    }
}

// ── _UnloadModule ─────────────────────────────────────────────────────────[...]
void MainWindow::_UnloadModule(LoadedModule& mod, bool dueToError,
                                const char* /*reason*/)
{
    // 1. Fall back to the empty view if this module's card was visible
    if (fModuleCardView->CardLayout() != nullptr &&
        fModuleCardView->CardLayout()->VisibleIndex() == mod.cardIndex)
        fModuleCardView->CardLayout()->SetVisibleItem((int32)0);

    // 2. Remove and destroy the content view
    if (mod.view) {
        fModuleCardView->RemoveChild(mod.view);
        delete mod.view;
        mod.view = nullptr;
    }

    // 3. Remove and destroy the sidebar button box
    if (mod.sidebarView) {
        fSidebarView->RemoveChild(mod.sidebarView);
        delete mod.sidebarView;
        mod.sidebarView = nullptr;
    }

    // 4. Deregister the BHandler and free the module object
    if (mod.instance) {
        RemoveHandler(mod.instance);
        delete mod.instance;
        mod.instance = nullptr;
    }

    // 5. Unload the shared library
    if (mod.image >= 0) {
        unload_add_on(mod.image);
        mod.image = -1;
    }
}

// ── _AddModuleToUI ──────────────────────────────────────────────────────────[...]
void MainWindow::_AddModuleToUI(LoadedModule& loaded)
{
    BView*   moduleView = loaded.instance->GetInterfaceView();
    BBitmap* moduleIcon = loaded.instance->GetIcon();

    // Sidebar entry: 80×75 px fixed-size box with icon button + name label
    BGroupView* moduleBox = new BGroupView(B_VERTICAL, 2);
    moduleBox->SetExplicitMinSize(BSize(80, 75));
    moduleBox->SetExplicitMaxSize(BSize(80, 75));
    moduleBox->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    BButton* btn = new BButton("", new BMessage(MSG_SELECT_MODULE));
    btn->SetExplicitMinSize(BSize(40, 40));
    btn->SetExplicitMaxSize(BSize(40, 40));
    if (moduleIcon)
        btn->SetIcon(moduleIcon);

    BMessage* selectMsg = new BMessage(MSG_SELECT_MODULE);
    selectMsg->AddString("signature", loaded.instance->Signature());
    btn->SetMessage(selectMsg);

    BStringView* nameLabel = new BStringView("name_label",
                                              loaded.instance->Name());
    nameLabel->SetAlignment(B_ALIGN_CENTER);
    BFont f;
    nameLabel->GetFont(&f);
    f.SetSize(9.0f); // Smaller font to fit the narrow sidebar
    nameLabel->SetFont(&f, B_FONT_SIZE);

    moduleBox->GroupLayout()->AddView(btn);
    moduleBox->GroupLayout()->AddView(nameLabel);

    // Insert before the trailing glue item
    int32 count = fSidebarView->GroupLayout()->CountItems();
    fSidebarView->GroupLayout()->AddView(count - 1, moduleBox);

    // Add the content view to the card layout
    fModuleCardView->AddChild(moduleView);
    loaded.cardIndex  = fModuleCardView->CountChildren() - 1;
    loaded.sidebarView = moduleBox;
    loaded.view        = moduleView;
}

// ── _SelectModule ─────────────────────────────────────────────────────────[...]
void MainWindow::_SelectModule(const BString& signature)
{
    for (const auto& mod : fModules) {
        if (!mod.disabled && mod.signature == signature) {
            if (fModuleCardView->CardLayout())
                fModuleCardView->CardLayout()->SetVisibleItem(mod.cardIndex);
            return;
        }
    }
}

// ── _SelectFirstActiveModule ──────────────────────────────────────────────────
void MainWindow::_SelectFirstActiveModule()
{
    for (const auto& mod : fModules) {
        if (!mod.disabled && mod.instance) {
            _SelectModule(mod.signature);
            return;
        }
    }
    // No active module: show the empty placeholder
    if (fModuleCardView->CardLayout())
        fModuleCardView->CardLayout()->SetVisibleItem((int32)0);
}

// ── DispatchMessage ─────────────────────────────────────────────────────────[...]
void MainWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
    BaseModule* module = _FindModuleForHandler(handler);
    if (!module) {
        BWindow::DispatchMessage(message, handler);
        return;
    }

    // Ignore messages for modules that have already been disabled
    for (const auto& mod : fModules) {
        if (mod.instance == module) {
            if (mod.disabled)
                return;
            break;
        }
    }

    // Arm the watchdog tracker
    fCurrentDispatchModule.store(module);
    fDispatchStartTime.store(system_time());

    if (sigsetjmp(fJumpBuf, 1) == 0) {
        // Normal dispatch path
        fCanJump.store(true);
        try {
            BWindow::DispatchMessage(message, handler);
        } catch (const std::exception& e) {
            fCanJump.store(false);
            fCurrentDispatchModule.store(nullptr);
            fDispatchStartTime.store(0);
            BString reason("C++ Exception: ");
            reason << e.what();
            _DisableModule(module, reason.String());
            return;
        } catch (...) {
            fCanJump.store(false);
            fCurrentDispatchModule.store(nullptr);
            fDispatchStartTime.store(0);
            _DisableModule(module, "Unknown C++ Exception");
            return;
        }
        fCanJump.store(false);
    } else {
        // Watchdog triggered siglongjmp — recover gracefully
        fCanJump.store(false);
        fCurrentDispatchModule.store(nullptr);
        fDispatchStartTime.store(0);
        _DisableModule(module, "Watchdog: UI thread hang detected");
        return;
    }

    fCurrentDispatchModule.store(nullptr);
    fDispatchStartTime.store(0);
}

// ── _FindModuleForHandler ─────────────────────────────────────────────────────
BaseModule* MainWindow::_FindModuleForHandler(BHandler* handler)
{
    if (!handler)
        return nullptr;

    // Direct match: the handler IS the module
    for (const auto& mod : fModules)
        if (mod.instance == handler)
            return mod.instance;

    // Walk up the view hierarchy to find which module owns this view
    BView* view = dynamic_cast<BView*>(handler);
    while (view) {
        for (const auto& mod : fModules)
            if (mod.view == view)
                return mod.instance;
        view = view->Parent();
    }

    return nullptr;
}

// ── _DisableModule ──────────────────────────────────────────────────────────[...]
void MainWindow::_DisableModule(BaseModule* module, const char* reason)
{
    if (!module)
        return;

    for (auto& mod : fModules) {
        if (mod.instance != module)
            continue;
        if (mod.disabled)
            return; // Already handled

        mod.disabled = true;

        BString moduleName(module->Name());
        BString moduleFileName(mod.fileName);
        BString disableReason(reason);

        // Log the deactivation event
        _WriteDeactivationLog(module, reason);

        // Show the red warning banner with a concise message
        if (fWarningBanner)
            fWarningBanner->AddDeactivatedModule(moduleName.String(),
                                                  disableReason.String());

        // Rename the .so file to .so.disabled on disk
        _RenameModuleFileDisabled(moduleFileName);

        // Remove module views and unload the library
        if (Lock()) {
            _UnloadModule(mod, true, reason);
            Unlock();
        }

        // Inform the user with a concise modal dialog
        BString alertMsg;
        alertMsg << "Module '" << moduleName << "' was deactivated for safety.\n\n"
                 << "Reason: " << disableReason;
        BAlert* alert = new BAlert("Module Deactivated", alertMsg.String(), "OK",
                                    nullptr, nullptr, B_WIDTH_AS_USUAL,
                                    B_WARNING_ALERT);
        alert->Go();
        break;
    }
}

// ── _RenameModuleFileDisabled ─────────────────────────────────────────────────
// Renames FileName.so → FileName.so.disabled in the add-ons directory.
void MainWindow::_RenameModuleFileDisabled(const BString& fileName)
{
    BPath dir = _GetModulesDirectory();
    BPath filePath = dir;
    filePath.Append(fileName.String());

    BEntry entry(filePath.Path());
    if (entry.InitCheck() != B_OK)
        return;

    BString newName(fileName);
    newName << ".disabled";
    entry.Rename(newName.String());
}

// ── _WriteDeactivationLog ─────────────────────────────────────────────────────
void MainWindow::_WriteDeactivationLog(BaseModule* module, const char* reason)
{
    BPath logsPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &logsPath) != B_OK)
        return;

    logsPath.Append("BeGin/logs");
    create_directory(logsPath.Path(), 0755);
    logsPath.Append("deactivations.log");

    std::ofstream log(logsPath.Path(), std::ios::app);
    if (!log.is_open())
        return;

    auto now   = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);

    log << "========================================\n"
        << "MODULE DEACTIVATED: "
        << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S") << "\n"
        << "Name:      " << module->Name()      << "\n"
        << "Signature: " << module->Signature() << "\n"
        << "Version:   " << module->Version()   << "\n"
        << "Reason:    " << reason               << "\n"
        << "----------------------------------------\n"
        << "Debugging tips:\n";

    BString r(reason);
    if (r.FindFirst("Watchdog") >= 0 || r.FindFirst("hang") >= 0) {
        log << "  * The module is running a blocking operation on the UI thread.\n"
            << "  * Move long-running work to a separate thread (std::thread / BThread).\n";
    } else if (r.FindFirst("Exception") >= 0 || r.FindFirst("exception") >= 0) {
        log << "  * The module threw an unhandled C++ exception.\n"
            << "  * Check memory allocations, null pointer dereferences, and index bounds.\n";
    } else {
        log << "  * An unexpected error occurred inside the module.\n";
    }
    log << "========================================\n\n";
}

// ── _WatchdogEntry ──────────────────────────────────────────────────────────[...]
int32 MainWindow::_WatchdogEntry(void* data)
{
    return static_cast<MainWindow*>(data)->_WatchdogLoop();
}

// ── _WatchdogLoop ─────────────────────────────────────────────────────────[...]
int32 MainWindow::_WatchdogLoop()
{
    while (fWatchdogRunning.load()) {
        snooze(100000); // Poll every 100 ms

        BaseModule* activeMod = fCurrentDispatchModule.load();
        bigtime_t   startTime = fDispatchStartTime.load();

        if (activeMod && startTime > 0) {
            bigtime_t elapsed = system_time() - startTime;
            if (elapsed > 1000000) { // More than 1 second → hang detected
                send_signal(fLooperThread, SIGUSR1);
                snooze(100000); // Give the UI thread time to jump
            }
        }
    }
    return 0;
}

// ── _SignalHandler ──────────────────────────────────────────────────────────[...]
// Executed on the UI thread when SIGUSR1 arrives from the watchdog.
void MainWindow::_SignalHandler(int sig)
{
    if (sig != SIGUSR1)
        return;

    // Only jump from the correct looper thread to prevent stack corruption
    if (sActiveWindow &&
        sActiveWindow->fCanJump.load() &&
        find_thread(nullptr) == sActiveWindow->fLooperThread) {
        siglongjmp(sActiveWindow->fJumpBuf, 1);
    }
}
