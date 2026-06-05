#include "MainWindow.h"

#include <Alert.h>
#include <Button.h>
#include <CardLayout.h>
#include <LayoutBuilder.h>
#include <SpaceLayoutItem.h>
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

// Define static member
MainWindow* MainWindow::sActiveWindow = nullptr;

MainWindow::MainWindow()
    : BWindow(BRect(100, 100, 800, 600), "BeGin", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
      fMenuBar(nullptr),
      fWarningBanner(nullptr),
      fSidebarView(nullptr),
      fCardView(nullptr),
      fEmptyView(nullptr),
      fFilePanel(nullptr),
      fWatchdogThread(-1),
      fLooperThread(-1),
      fCurrentDispatchModule(nullptr),
      fDispatchStartTime(0),
      fWatchdogRunning(true),
      fCanJump(false)
{
    sActiveWindow = this;
    fLooperThread = Thread();

    // Register POSIX signal handler for Watchdog interrupts
    struct sigaction sa;
    sa.sa_handler = _SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, nullptr);

    // Initialize modules directory & scan for existing add-ons
    _InitModulesDirectory();
    _InitInterface();

    // Spawn and run the watchdog thread
    fWatchdogThread = spawn_thread(_WatchdogEntry, "watchdog_thread", B_NORMAL_PRIORITY, this);
    if (fWatchdogThread >= 0) {
        resume_thread(fWatchdogThread);
    }

    // Load modules that are already present in the folder at startup
    _LoadExistingModules();

    // Automatically select the first successfully loaded module
    for (const auto& mod : fModules) {
        if (!mod.disabled && mod.instance != nullptr) {
            _SelectModule(mod.signature);
            break;
        }
    }
}

MainWindow::~MainWindow()
{
    // Stop directory monitoring
    stop_watching(this);

    // Terminate watchdog thread
    fWatchdogRunning.store(false);
    if (fWatchdogThread >= 0) {
        status_t exitVal;
        wait_for_thread(fWatchdogThread, &exitVal);
    }

    // Clean up FilePanel
    delete fFilePanel;

    if (sActiveWindow == this) {
        sActiveWindow = nullptr;
    }
}

void MainWindow::Show()
{
    BWindow::Show();

    // Start watching the modules folder for live updates (now that Looper is active!)
    BPath path = _GetModulesDirectory();
    BDirectory dir(path.Path());
    if (dir.InitCheck() == B_OK) {
        dir.GetNodeRef(&fModulesDirNodeRef);
        watch_node(&fModulesDirNodeRef, B_WATCH_DIRECTORY, this);
    }
}

void MainWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_FILE_EXIT:
            PostMessage(B_QUIT_REQUESTED);
            break;

        case MSG_LOAD_MODULE_PROMPT:
        {
            if (fFilePanel == nullptr) {
                fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
                                            nullptr, B_FILE_NODE, false, nullptr);
            }
            fFilePanel->Show();
            break;
        }

        case B_REFS_RECEIVED:
        {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) {
                _LoadModule(ref);
            }
            break;
        }

        case MSG_SELECT_MODULE:
        {
            BString signature;
            if (message->FindString("signature", &signature) == B_OK) {
                _SelectModule(signature);
            }
            break;
        }

        case B_NODE_MONITOR:
        {
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) == B_OK) {
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
                    const char* name;
                    if (message->FindString("name", &name) == B_OK) {
                        _UnloadModuleByName(name);
                    }
                }
            }
            break;
        }

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool MainWindow::QuitRequested()
{
    // Stop directory monitoring to prevent incoming messages during shutdown
    stop_watching(this);

    // Memory Safety: unload all modules dynamically and cleanly
    for (auto& loaded : fModules) {
        _UnloadModule(loaded, false, nullptr);
    }
    fModules.clear();

    return true;
}

void MainWindow::_InitInterface()
{
    // Menu Bar
    fMenuBar = new BMenuBar("menubar");
    BMenu* fileMenu = new BMenu("File");
    fileMenu->AddItem(new BMenuItem("Esci", new BMessage(MSG_FILE_EXIT), 'Q'));
    fMenuBar->AddItem(fileMenu);

    BMenu* modulesMenu = new BMenu("Moduli");
    modulesMenu->AddItem(new BMenuItem("Carica modulo...", new BMessage(MSG_LOAD_MODULE_PROMPT), 'L'));
    fMenuBar->AddItem(modulesMenu);

    // Red warning banner for deactivated modules
    fWarningBanner = new WarningBanner("warning_banner");

    // Left navigation column (vertical sidebar) with fixed width
    fSidebarView = new BGroupView(B_VERTICAL, 10);
    fSidebarView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    
    // Set Sidebar fixed width to 90px
    fSidebarView->SetExplicitMinSize(BSize(90, 0));
    fSidebarView->SetExplicitMaxSize(BSize(90, 10000));
    
    // Add glue so module buttons align to the top of the sidebar
    fSidebarView->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());

    // Right content area (cards)
    fCardView = new BCardView();
    fEmptyView = new EmptyView("empty_view");
    fCardView->AddChild(fEmptyView);

    // Layout configuration using standard BLayoutBuilder
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .Add(fWarningBanner)         // Add banner below the menu bar
        .AddSplit(B_HORIZONTAL, 2.0f) // horizontal split view
            .Add(fSidebarView, 1.0f)  // left sidebar column
            .Add(fCardView, 4.0f)     // right content card area
        .End();
}

BPath MainWindow::_GetModulesDirectory()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("BeGin/add-ons");
    }
    return path;
}

void MainWindow::_InitModulesDirectory()
{
    BPath path = _GetModulesDirectory();
    create_directory(path.Path(), 0777);
}

void MainWindow::_LoadExistingModules()
{
    BPath path = _GetModulesDirectory();
    BDirectory dir(path.Path());
    if (dir.InitCheck() != B_OK) return;

    entry_ref ref;
    while (dir.GetNextRef(&ref) == B_OK) {
        BString filename(ref.name);
        if (filename.EndsWith(".so")) {
            _LoadModule(ref);
        }
    }
}

void MainWindow::_LoadModule(const entry_ref& ref)
{
    BEntry entry(&ref, true);
    BPath sourcePath;
    if (entry.GetPath(&sourcePath) != B_OK) {
        BAlert* alert = new BAlert("Errore", "Impossibile recuperare il percorso del modulo.", "OK",
                                   nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    BPath destDir = _GetModulesDirectory();
    BPath destPath = destDir;
    destPath.Append(ref.name);

    BString fileName(ref.name);

    // Copy to modules settings folder if it's not already there
    if (strcmp(sourcePath.Path(), destPath.Path()) != 0) {
        try {
            std::filesystem::copy_file(sourcePath.Path(), destPath.Path(), std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            BString errText = "Impossibile copiare il modulo nella cartella di destinazione: ";
            errText << e.what();
            BAlert* alert = new BAlert("Errore", errText.String(), "OK",
                                       nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
            alert->Go();
            return;
        }
    }

    // Load add-on into memory
    image_id image = load_add_on(destPath.Path());
    if (image < 0) {
        BAlert* alert = new BAlert("Errore", "Impossibile caricare il modulo .so selezionato.", "OK",
                                   nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Retrieve factory function
    instantiate_module_func instantiateFunc = nullptr;
    status_t symStatus = get_image_symbol(image, "instantiate_module", B_SYMBOL_TYPE_TEXT, (void**)&instantiateFunc);
    if (symStatus != B_OK || instantiateFunc == nullptr) {
        unload_add_on(image);
        BAlert* alert = new BAlert("Errore", "Il modulo caricato non esporta la funzione 'instantiate_module'.", "OK",
                                   nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Instantiate module
    BaseModule* instance = instantiateFunc();
    if (instance == nullptr) {
        unload_add_on(image);
        BAlert* alert = new BAlert("Errore", "Impossibile istanziare il modulo.", "OK",
                                   nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    // Verify if signature is already present (avoid duplicates or replace deactivated module)
    BString signature(instance->Signature());
    for (auto it = fModules.begin(); it != fModules.end(); ) {
        if (it->fileName == fileName || it->signature == signature) {
            if (it->disabled) {
                // Remove the old deactivated instance record cleanly from our list
                it = fModules.erase(it);
            } else {
                // Active module duplicate: prevent load
                delete instance;
                unload_add_on(image);
                BAlert* alert = new BAlert("Errore", "Un modulo con la stessa firma è già caricato.", "OK",
                                           nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
                alert->Go();
                return;
            }
        } else {
            ++it;
        }
    }

    // Register module handler in Window looper
    AddHandler(instance);

    // Keep track of the loaded module
    LoadedModule loaded;
    loaded.image = image;
    loaded.instance = instance;
    loaded.sidebarView = nullptr;
    loaded.view = nullptr;
    loaded.cardIndex = -1;
    loaded.disabled = false;
    loaded.fileName = fileName;
    loaded.signature = signature;

    _AddModuleToUI(loaded);
    fModules.push_back(loaded);

    // Auto-select if it is the only active module loaded
    int32 activeCount = 0;
    LoadedModule* onlyActive = nullptr;
    for (auto& mod : fModules) {
        if (!mod.disabled && mod.instance != nullptr) {
            activeCount++;
            onlyActive = &mod;
        }
    }
    if (activeCount == 1 && onlyActive != nullptr) {
        _SelectModule(onlyActive->signature);
    }
}

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

void MainWindow::_UnloadModule(LoadedModule& mod, bool dueToError, const char* reason)
{
    // 1. Fallback visible card to EmptyView if this module's view was visible
    if (fCardView->CardLayout() != nullptr && fCardView->CardLayout()->VisibleIndex() == mod.cardIndex) {
        fCardView->CardLayout()->SetVisibleItem((int32)0);
    }

    // 2. Detach and delete graphic view
    if (mod.view != nullptr) {
        fCardView->RemoveChild(mod.view);
        delete mod.view;
        mod.view = nullptr;
    }

    // 3. Detach and delete sidebar vertical box
    if (mod.sidebarView != nullptr) {
        fSidebarView->RemoveChild(mod.sidebarView);
        delete mod.sidebarView;
        mod.sidebarView = nullptr;
    }

    // 4. Unregister BHandler from window
    if (mod.instance != nullptr) {
        RemoveHandler(mod.instance);
        delete mod.instance;
        mod.instance = nullptr;
    }

    // 5. Unload add-on library from memory
    if (mod.image >= 0) {
        unload_add_on(mod.image);
        mod.image = -1;
    }
}

void MainWindow::_AddModuleToUI(LoadedModule& loaded)
{
    // Retrieve interface and icon
    BView* moduleView = loaded.instance->GetInterfaceView();
    BBitmap* moduleIcon = loaded.instance->GetIcon();

    // Create a vertical box view for the sidebar entry
    BGroupView* moduleBox = new BGroupView(B_VERTICAL, 2);
    moduleBox->SetExplicitMinSize(BSize(80, 75));
    moduleBox->SetExplicitMaxSize(BSize(80, 75));
    moduleBox->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // Create square navigation button for the icon
    BButton* button = new BButton("", new BMessage(MSG_SELECT_MODULE));
    button->SetExplicitMinSize(BSize(40, 40));
    button->SetExplicitMaxSize(BSize(40, 40));
    
    if (moduleIcon != nullptr) {
        button->SetIcon(moduleIcon);
    }

    // Attach message payload
    BMessage* selectMsg = new BMessage(MSG_SELECT_MODULE);
    selectMsg->AddString("signature", loaded.instance->Signature());
    button->SetMessage(selectMsg);

    // Create label under the icon button
    BStringView* nameLabel = new BStringView("name_label", loaded.instance->Name());
    nameLabel->SetAlignment(B_ALIGN_CENTER);
    
    BFont labelFont;
    nameLabel->GetFont(&labelFont);
    labelFont.SetSize(9.0f); // Small size to fit the sidebar width
    nameLabel->SetFont(&labelFont, B_FONT_SIZE);

    // Assemble the module box layout
    moduleBox->GroupLayout()->AddView(button);
    moduleBox->GroupLayout()->AddView(nameLabel);

    // Add module box to sidebar view, positioning it before the bottom glue
    int32 itemCount = fSidebarView->GroupLayout()->CountItems();
    fSidebarView->GroupLayout()->AddView(itemCount - 1, moduleBox);

    // Add view to card layout and assign card index
    fCardView->AddChild(moduleView);
    loaded.cardIndex = fCardView->CountChildren() - 1;

    loaded.sidebarView = moduleBox;
    loaded.view = moduleView;
}

void MainWindow::_SelectModule(const BString& signature)
{
    for (const auto& mod : fModules) {
        if (!mod.disabled && mod.signature == signature) {
            if (fCardView->CardLayout() != nullptr) {
                fCardView->CardLayout()->SetVisibleItem(mod.cardIndex);
            }
            return;
        }
    }
}

void MainWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
    BaseModule* module = _FindModuleForHandler(handler);
    if (module != nullptr) {
        // Block processing if the module has already been disabled
        for (const auto& mod : fModules) {
            if (mod.instance == module) {
                if (mod.disabled) {
                    return; // Ignore message
                }
                break;
            }
        }

        // Set tracking variables for Watchdog monitoring
        fCurrentDispatchModule.store(module);
        fDispatchStartTime.store(system_time());

        // Setup sigsetjmp for safe asynchronous watchdog recovery
        if (sigsetjmp(fJumpBuf, 1) == 0) {
            fCanJump.store(true);
            
            try {
                BWindow::DispatchMessage(message, handler);
            } catch (const std::exception& e) {
                fCanJump.store(false);
                fCurrentDispatchModule.store(nullptr);
                fDispatchStartTime.store(0);
                _DisableModule(module, (BString("Eccezione C++: ") << e.what()).String());
                return;
            } catch (...) {
                fCanJump.store(false);
                fCurrentDispatchModule.store(nullptr);
                fDispatchStartTime.store(0);
                _DisableModule(module, "Eccezione C++ sconosciuta");
                return;
            }
            
            fCanJump.store(false);
        } else {
            // Watchdog triggered siglongjmp!
            fCanJump.store(false);
            fCurrentDispatchModule.store(nullptr);
            fDispatchStartTime.store(0);
            
            _DisableModule(module, "Watchdog: Thread UI bloccato (sbloccato asincronamente)");
            return;
        }

        // Reset tracking variables
        fCurrentDispatchModule.store(nullptr);
        fDispatchStartTime.store(0);
    } else {
        BWindow::DispatchMessage(message, handler);
    }
}

BaseModule* MainWindow::_FindModuleForHandler(BHandler* handler)
{
    if (handler == nullptr) return nullptr;

    // Check if direct handler is a module
    for (const auto& mod : fModules) {
        if (mod.instance == handler) {
            return mod.instance;
        }
    }

    // Check if handler is a BView that resides within a module's interface view
    BView* view = dynamic_cast<BView*>(handler);
    while (view != nullptr) {
        for (const auto& mod : fModules) {
            if (mod.view == view) {
                return mod.instance;
            }
        }
        view = view->Parent();
    }

    return nullptr;
}

void MainWindow::_DisableModule(BaseModule* module, const char* reason)
{
    if (module == nullptr) return;

    for (auto& mod : fModules) {
        if (mod.instance == module) {
            if (mod.disabled) return; // Already disabled

            mod.disabled = true;

            // Save module metadata before destruction
            BString moduleName(module->Name());
            BString disableReason(reason);

            // 1. Write debug diagnostics log
            _WriteDeactivationLog(module, reason);

            // 2. Alert user via warning banner
            if (fWarningBanner != nullptr) {
                fWarningBanner->AddDeactivatedModule(moduleName.String(), disableReason.String());
            }

            // 3. Clean up GUI views & unload add-on module
            if (Lock()) {
                _UnloadModule(mod, true, reason);
                Unlock();
            }

            // Show standard modal warning dialog
            BString alertMsg;
            alertMsg << "Il modulo '" << moduleName << "' è stato disattivato per motivi di sicurezza.\n\nCausa: " << disableReason;
            BAlert* alert = new BAlert("Modulo Disattivato", alertMsg.String(), "OK",
                                       nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            alert->Go();
            
            break;
        }
    }
}

void MainWindow::_WriteDeactivationLog(BaseModule* module, const char* reason)
{
    BPath logsPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &logsPath) == B_OK) {
        logsPath.Append("BeGin/logs");
        create_directory(logsPath.Path(), 0777);
        logsPath.Append("deactivations.log");

        std::ofstream logFile(logsPath.Path(), std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            
            logFile << "========================================\n";
            logFile << "DISATTIVAZIONE MODULO: " << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S") << "\n";
            logFile << "Nome:                  " << module->Name() << "\n";
            logFile << "Firma (Signature):     " << module->Signature() << "\n";
            logFile << "Causa della Scelta:    " << reason << "\n";
            logFile << "----------------------------------------\n";
            logFile << "CONSIGLI PER IL DEBUG:\n";
            if (strcmp(reason, "Watchdog: Thread UI bloccato per >1s") == 0 ||
                strcmp(reason, "Watchdog: Rilevato blocco prolungato dell'interfaccia") == 0 ||
                strcmp(reason, "Watchdog: Thread UI bloccato (sbloccato asincronamente)") == 0) {
                logFile << "  * Il modulo esegue un loop infinito o calcoli pesanti nel thread principale.\n";
                logFile << "  * Sposta le operazioni bloccanti in un thread separato (std::thread o BThread).\n";
            } else if (strncmp(reason, "Eccezione C++:", 14) == 0) {
                logFile << "  * Il modulo ha sollevato un'eccezione standard non gestita.\n";
                logFile << "  * Controlla i punti in cui allochi risorse, dereferenzi puntatori o accedi a indici.\n";
            } else {
                logFile << "  * Si è verificato un errore generico o un'eccezione sconosciuta.\n";
            }
            logFile << "========================================\n\n";
            logFile.close();
        }
    }
}

int32 MainWindow::_WatchdogEntry(void* data)
{
    MainWindow* window = static_cast<MainWindow*>(data);
    return window->_WatchdogLoop();
}

int32 MainWindow::_WatchdogLoop()
{
    while (fWatchdogRunning.load()) {
        snooze(100000); // Check every 100 milliseconds
        
        BaseModule* activeMod = fCurrentDispatchModule.load();
        bigtime_t startTime = fDispatchStartTime.load();
        
        if (activeMod != nullptr && startTime > 0) {
            bigtime_t elapsed = system_time() - startTime;
            if (elapsed > 1000000) { // UI blocked for more than 1 second
                // Interrupt the UI thread using SIGUSR1 POSIX signal
                send_signal(fLooperThread, SIGUSR1);
                
                // Allow the thread to intercept signal and execute siglongjmp
                snooze(100000);
            }
        }
    }
    return 0;
}

void MainWindow::_SignalHandler(int sig)
{
    if (sig == SIGUSR1) {
        if (sActiveWindow != nullptr && sActiveWindow->fCanJump.load()) {
            siglongjmp(sActiveWindow->fJumpBuf, 1);
        }
    }
}
