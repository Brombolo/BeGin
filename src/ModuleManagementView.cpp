#include "ModuleManagementView.h"
#include "MainWindow.h"

#include <Directory.h>
#include <FindDirectory.h>
#include <Alert.h>
#include <LayoutBuilder.h>
#include <string.h>

ModuleManagementView::ModuleManagementView(MainWindow* parent)
    : BView("Gestione Moduli", B_WILL_DRAW),
      fParent(parent),
      fListView(nullptr),
      fScrollView(nullptr),
      fDetailsContainer(nullptr),
      fNameLabel(nullptr),
      fFileLabel(nullptr),
      fStatusLabel(nullptr),
      fToggleBtn(nullptr),
      fReloadBtn(nullptr),
      fDeleteBtn(nullptr)
{
    _InitInterface();
}

ModuleManagementView::~ModuleManagementView()
{
}

void ModuleManagementView::AttachedToWindow()
{
    BView::AttachedToWindow();
    fListView->SetTarget(this);
    fToggleBtn->SetTarget(this);
    fReloadBtn->SetTarget(this);
    fDeleteBtn->SetTarget(this);
    ScanModules();
}

void ModuleManagementView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case 'slch': // Selection Changed
            _UpdateDetails();
            break;
        case 'tggl': // Toggle module
            _ToggleModule();
            break;
        case 'rldm': // Reload module
            _ReloadModule();
            break;
        case 'dltm': // Delete module
            _DeleteModule();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}

void ModuleManagementView::ScanModules()
{
    if (LockLooper()) {
        // Clear list first
        int32 count = fListView->CountItems();
        for (int32 i = count - 1; i >= 0; --i) {
            BListItem* item = fListView->RemoveItem(i);
            delete item;
        }
        fModuleFiles.clear();

        BPath path;
        if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
            path.Append("BeGin/add-ons");

            BDirectory dir(path.Path());
            if (dir.InitCheck() == B_OK) {
                entry_ref ref;
                while (dir.GetNextRef(&ref) == B_OK) {
                    BString fileName(ref.name);
                    bool isActive = fileName.EndsWith(".so");
                    bool isDisabled = fileName.EndsWith(".so.disabled");
                    if (isActive || isDisabled) {
                        ModuleFileItem item;
                        item.ref = ref;
                        item.fileName = fileName;
                        item.isActive = isActive;
                        fModuleFiles.push_back(item);

                        BString display = fileName;
                        if (isDisabled) {
                            display << " [Disattivato]";
                        }
                        fListView->AddItem(new BStringItem(display.String()));
                    }
                }
            }
        }

        _UpdateDetails();
        UnlockLooper();
    }
}

void ModuleManagementView::_InitInterface()
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    fListView = new BListView("module_files_list");
    fListView->SetSelectionMessage(new BMessage('slch'));
    fScrollView = new BScrollView("scroll_modules", fListView, 0, false, true);

    fDetailsContainer = new BView("details", B_WILL_DRAW);
    fDetailsContainer->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    fNameLabel = new BStringView("name_lbl", "Seleziona un modulo");
    fFileLabel = new BStringView("file_lbl", "");
    fStatusLabel = new BStringView("status_lbl", "");

    BFont boldFont;
    fNameLabel->GetFont(&boldFont);
    boldFont.SetFace(B_BOLD_FACE);
    boldFont.SetSize(12.0f);
    fNameLabel->SetFont(&boldFont, B_FONT_FACE | B_FONT_SIZE);

    fToggleBtn = new BButton("Attiva / Disattiva", new BMessage('tggl'));
    fReloadBtn = new BButton("Ricarica", new BMessage('rldm'));
    fDeleteBtn = new BButton("Elimina", new BMessage('dltm'));

    fToggleBtn->SetEnabled(false);
    fReloadBtn->SetEnabled(false);
    fDeleteBtn->SetEnabled(false);

    BLayoutBuilder::Group<>(fDetailsContainer, B_VERTICAL, 10)
        .SetInsets(10, 10, 10, 10)
        .Add(fNameLabel)
        .Add(fFileLabel)
        .Add(fStatusLabel)
        .AddGlue()
        .AddGroup(B_HORIZONTAL, 10)
            .Add(fToggleBtn)
            .Add(fReloadBtn)
            .Add(fDeleteBtn)
        .End()
    .End();

    BLayoutBuilder::Group<>(this, B_HORIZONTAL, 10)
        .SetInsets(10, 10, 10, 10)
        .Add(fScrollView, 2.0f)
        .Add(fDetailsContainer, 3.0f)
    .End();
}

void ModuleManagementView::_UpdateDetails()
{
    int32 selection = fListView->CurrentSelection();
    if (selection < 0 || selection >= (int32)fModuleFiles.size()) {
        fNameLabel->SetText("Seleziona un modulo");
        fFileLabel->SetText("");
        fStatusLabel->SetText("");
        fToggleBtn->SetEnabled(false);
        fReloadBtn->SetEnabled(false);
        fDeleteBtn->SetEnabled(false);
        return;
    }

    const auto& item = fModuleFiles[selection];
    fNameLabel->SetText(item.fileName.String());
    
    BPath path(&item.ref);
    fFileLabel->SetText(path.Path());

    if (item.isActive) {
        fStatusLabel->SetText("Stato: Attivo");
        fToggleBtn->SetLabel("Disattiva");
        fToggleBtn->SetEnabled(true);
        fReloadBtn->SetEnabled(true);
    } else {
        fStatusLabel->SetText("Stato: Disattivato");
        fToggleBtn->SetLabel("Attiva");
        fToggleBtn->SetEnabled(true);
        fReloadBtn->SetEnabled(false);
    }
    fDeleteBtn->SetEnabled(true);
}

void ModuleManagementView::_ToggleModule()
{
    int32 selection = fListView->CurrentSelection();
    if (selection < 0 || selection >= (int32)fModuleFiles.size()) return;

    auto& item = fModuleFiles[selection];
    BEntry entry(&item.ref);
    if (entry.InitCheck() != B_OK) return;

    BString newName = item.fileName;
    if (item.isActive) {
        newName << ".disabled";
    } else {
        if (newName.EndsWith(".so.disabled")) {
            newName.Truncate(newName.Length() - 9);
        }
    }

    status_t err = entry.Rename(newName.String());
    if (err != B_OK) {
        BString errText = "Errore durante la rinomina del file: ";
        errText << strerror(err);
        BAlert* alert = new BAlert("Errore", errText.String(), "OK",
                                   nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
        return;
    }

    ScanModules();
    
    for (size_t i = 0; i < fModuleFiles.size(); ++i) {
        if (fModuleFiles[i].fileName == newName) {
            fListView->Select(i);
            break;
        }
    }
}

void ModuleManagementView::_ReloadModule()
{
    int32 selection = fListView->CurrentSelection();
    if (selection < 0 || selection >= (int32)fModuleFiles.size()) return;

    const auto& item = fModuleFiles[selection];
    if (!item.isActive) return;

    fParent->UnloadModuleByName(item.fileName.String());
    fParent->LoadModule(item.ref);

    ScanModules();
    
    for (size_t i = 0; i < fModuleFiles.size(); ++i) {
        if (fModuleFiles[i].fileName == item.fileName) {
            fListView->Select(i);
            break;
        }
    }
}

void ModuleManagementView::_DeleteModule()
{
    int32 selection = fListView->CurrentSelection();
    if (selection < 0 || selection >= (int32)fModuleFiles.size()) return;

    const auto& item = fModuleFiles[selection];

    BAlert* alert = new BAlert("Elimina modulo",
                               (BString("Sei sicuro di voler eliminare permanentemente il modulo '") << item.fileName << "'?").String(),
                               "Annulla", "Elimina", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    if (alert->Go() != 1) return;

    if (item.isActive) {
        fParent->UnloadModuleByName(item.fileName.String());
    }

    BEntry entry(&item.ref);
    status_t err = entry.Remove();
    if (err != B_OK) {
        BString errText = "Impossibile eliminare il file: ";
        errText << strerror(err);
        BAlert* errAlert = new BAlert("Errore", errText.String(), "OK",
                                      nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        errAlert->Go();
    }

    ScanModules();
}
