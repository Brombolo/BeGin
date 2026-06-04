#include "MainWindow.h"

#include <Alert.h>
#include <Button.h>
#include <CardLayout.h>
#include <LayoutBuilder.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <String.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>

MainWindow::MainWindow()
    : BWindow(BRect(100, 100, 800, 600), "BeGin", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
      fMenuBar(nullptr),
      fWarningBanner(nullptr),
      fSidebarView(nullptr),
      fCardView(nullptr),
      fEmptyView(nullptr),
      fFilePanel(nullptr)
{
    _InitModulesDirectory();
    _InitInterface();
}

MainWindow::~MainWindow()
{
    // Clean up FilePanel
    delete fFilePanel;
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

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool MainWindow::QuitRequested()
{
    // Memory Safety: unload all modules dynamically and cleanly
    for (auto& loaded : fModules) {
        // 1. Remove and destroy interface elements if they are still active
        if (loaded.view != nullptr) {
            fCardView->RemoveChild(loaded.view);
            delete loaded.view;
        }
        if (loaded.button != nullptr) {
            fSidebarView->RemoveChild(loaded.button);
            delete loaded.button;
        }

        // 2. Remove module from window's BHandler registry and delete it if not already done
        if (loaded.instance != nullptr) {
            RemoveHandler(loaded.instance);
            delete loaded.instance;
        }

        // 3. Unload the add-on from memory
        unload_add_on(loaded.image);
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

    // Left navigation column (vertical sidebar)
    fSidebarView = new BGroupView(B_VERTICAL, 5);
    fSidebarView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    
    // Add glue so module buttons align to the top of the sidebar
    fSidebarView->GroupLayout()->AddGlue();

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

    // Verify if signature is already present (avoid duplicates)
    for (const auto& mod : fModules) {
        if (strcmp(mod.instance->Signature(), instance->Signature()) == 0) {
            delete instance;
            unload_add_on(image);
            BAlert* alert = new BAlert("Errore", "Un modulo con la stessa firma è già caricato.", "OK",
                                       nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
            alert->Go();
            return;
        }
    }

    // Register module handler
    AddHandler(instance);

    // Keep track of the loaded module
    LoadedModule loaded;
    loaded.image = image;
    loaded.instance = instance;
    loaded.button = nullptr;
    loaded.view = nullptr;
    loaded.cardIndex = -1;
    loaded.disabled = false;

    _AddModuleToUI(loaded);
    fModules.push_back(loaded);
}

void MainWindow::_AddModuleToUI(LoadedModule& loaded)
{
    // Retrieve interface and icon
    BView* moduleView = loaded.instance->GetInterfaceView();
    BBitmap* moduleIcon = loaded.instance->GetIcon();

    // Create square navigation button
    BButton* button = new BButton(loaded.instance->Name(), "", new BMessage(MSG_SELECT_MODULE));
    button->SetExplicitMinSize(BSize(48, 48));
    button->SetExplicitMaxSize(BSize(48, 48));
    
    if (moduleIcon != nullptr) {
        button->SetIcon(moduleIcon);
    }

    // Attach message payload
    BMessage* selectMsg = new BMessage(MSG_SELECT_MODULE);
    selectMsg->AddString("signature", loaded.instance->Signature());
    button->SetMessage(selectMsg);

    // Add button to sidebar view, positioning it before the bottom glue
    int32 viewsCount = fSidebarView->GroupLayout()->CountViews();
    fSidebarView->GroupLayout()->AddView(viewsCount - 1, button);

    // Add view to card layout and assign card index
    fCardView->AddChild(moduleView);
    loaded.cardIndex = fCardView->CountChildren() - 1;

    loaded.button = button;
    loaded.view = moduleView;
}

void MainWindow::_SelectModule(const BString& signature)
{
    for (const auto& mod : fModules) {
        if (mod.instance != nullptr && signature == mod.instance->Signature()) {
            if (!mod.disabled && fCardView->CardLayout() != nullptr) {
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

        // Measure execution time and catch C++ exceptions (Watchdog & Exception Trap)
        bigtime_t startTime = system_time();
        try {
            BWindow::DispatchMessage(message, handler);
        } catch (const std::exception& e) {
            _DisableModule(module, (BString("Eccezione C++: ") << e.what()).String());
            return;
        } catch (...) {
            _DisableModule(module, "Eccezione C++ sconosciuta");
            return;
        }

        bigtime_t duration = system_time() - startTime;
        if (duration > 1000000) { // More than 1 second (1,000,000 microseconds)
            _DisableModule(module, "Watchdog: Thread UI bloccato per >1s");
        }
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

            // 1. Write debug diagnostics log
            _WriteDeactivationLog(module, reason);

            // 2. Alert user via warning banner
            if (fWarningBanner != nullptr) {
                fWarningBanner->AddDeactivatedModule(module->Name(), reason);
            }

            // Lock Looper to ensure safe UI manipulation from current dispatch
            if (Lock()) {
                // 3. Fallback visible card to EmptyView if the disabled module was shown
                if (fCardView->CardLayout() != nullptr && fCardView->CardLayout()->VisibleIndex() == mod.cardIndex) {
                    fCardView->CardLayout()->SetVisibleItem((int32)0);
                }

                // 4. Detach UI components and delete them
                if (mod.view != nullptr) {
                    fCardView->RemoveChild(mod.view);
                    delete mod.view;
                    mod.view = nullptr;
                }
                if (mod.button != nullptr) {
                    fSidebarView->RemoveChild(mod.button);
                    delete mod.button;
                    mod.button = nullptr;
                }

                // 5. Unregister looper handler
                RemoveHandler(mod.instance);

                // 6. Delete module instance
                delete mod.instance;
                mod.instance = nullptr;

                // 7. Unload dynamic library
                unload_add_on(mod.image);

                Unlock();
            }

            // Show standard modal alert too
            BString alertMsg;
            alertMsg << "Il modulo '" << module->Name() << "' è stato disattivato per motivi di sicurezza.\n\nCausa: " << reason;
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
            if (strcmp(reason, "Watchdog: Thread UI bloccato per >1s") == 0) {
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
