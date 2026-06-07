// ModuleManagementView.cpp – Add-on module management settings panel
#include "ModuleManagementView.h"
#include "MainWindow.h"
#include "BaseModule.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Alert.h>
#include <LayoutBuilder.h>
#include <image.h>
#include <string.h>

// Internal message codes
enum {
    MSG_PANEL_LOAD_REF  = 'ldrf', // Sent by the file panel when a file is chosen
    MSG_LIST_SELECTION  = 'slch',
    MSG_TOGGLE_MODULE   = 'tggl',
    MSG_RELOAD_MODULE   = 'rldm',
    MSG_DELETE_MODULE   = 'dltm',
    MSG_OPEN_LOAD_PANEL = 'olpn',
};

// ── Constructor ──────────────────────────────────────────────────────────[...]
ModuleManagementView::ModuleManagementView(MainWindow* parent)
    : BView("Module Management", B_WILL_DRAW),
      fParent(parent),
      fListView(nullptr),
      fScrollView(nullptr),
      fLoadBtn(nullptr),
      fFilePanel(nullptr),
      fDetailsContainer(nullptr),
      fNameLabel(nullptr),
      fVersionLabel(nullptr),
      fFileLabel(nullptr),
      fStatusLabel(nullptr),
      fToggleBtn(nullptr),
      fReloadBtn(nullptr),
      fDeleteBtn(nullptr)
{
    _InitInterface();
}

// ── Destructor ──────────────────────────────────────────────────────────[...]
ModuleManagementView::~ModuleManagementView()
{
    delete fFilePanel;
}

// ── AttachedToWindow ────────────────────────────────────────────────────────[...]
void ModuleManagementView::AttachedToWindow()
{
    BView::AttachedToWindow();
    fListView->SetTarget(this);
    fLoadBtn->SetTarget(this);
    fToggleBtn->SetTarget(this);
    fReloadBtn->SetTarget(this);
    fDeleteBtn->SetTarget(this);
    ScanModules();
}

// ── MessageReceived ────────────────────────────────────────────────────────[...]
void ModuleManagementView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_LIST_SELECTION:
            _UpdateDetails();
            break;

        case MSG_OPEN_LOAD_PANEL:
            _LoadModule();
            break;

        case MSG_PANEL_LOAD_REF:
        {
            // File panel delivered a chosen file reference
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) {
                fParent->LoadModule(ref);
                ScanModules();
            }
            break;
        }

        case MSG_TOGGLE_MODULE:
            _ToggleModule();
            break;

        case MSG_RELOAD_MODULE:
            _ReloadModule();
            break;

        case MSG_DELETE_MODULE:
            _DeleteModule();
            break;

        default:
            BView::MessageReceived(message);
            break;
    }
}

// ── ScanModules ─────────────────────────────────────────────────────────[...]
void ModuleManagementView::ScanModules()
{
    if (!LockLooper())
        return;

    // Clear the list
    int32 count = fListView->CountItems();
    for (int32 i = count - 1; i >= 0; --i) {
        delete fListView->RemoveItem(i);
    }
    fModuleFiles.clear();

    // Scan the add-ons directory
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("BeGin/add-ons");

        BDirectory dir(path.Path());
        if (dir.InitCheck() == B_OK) {
            entry_ref ref;
            while (dir.GetNextRef(&ref) == B_OK) {
                BString fileName(ref.name);
                bool isActive   = fileName.EndsWith(".so");
                bool isDisabled = fileName.EndsWith(".so.disabled");

                if (!isActive && !isDisabled)
                    continue;

                ModuleFileItem item;
                item.ref      = ref;
                item.fileName = fileName;
                item.isActive = isActive;
                item.version  = _ReadModuleVersion(ref, isActive);
                fModuleFiles.push_back(item);

                BString display = fileName;
                if (isDisabled)
                    display << "  [Disabled]";
                fListView->AddItem(new BStringItem(display.String()));
            }
        }
    }

    _UpdateDetails();
    UnlockLooper();
}

// ── _InitInterface ──────────────────────────────────────────────────────[...]
void ModuleManagementView::_InitInterface()
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // ── Left panel: module list + load button ─────────────────────────────────
    fListView = new BListView("module_files_list");
    fListView->SetSelectionMessage(new BMessage(MSG_LIST_SELECTION));
    fScrollView = new BScrollView("scroll_modules", fListView,
                                   0, false, true);

    fLoadBtn = new BButton("load_btn", "Load Module…",
                            new BMessage(MSG_OPEN_LOAD_PANEL));

    BView* leftPanel = new BView("left_panel", 0);
    BLayoutBuilder::Group<>(leftPanel, B_VERTICAL, 6)
        .SetInsets(0)
        .Add(fScrollView, 1.0f)
        .Add(fLoadBtn)
        .End();

    // ── Right panel: details + action buttons ─────────────────────────────────
    fDetailsContainer = new BView("details", B_WILL_DRAW);
    fDetailsContainer->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    fNameLabel    = new BStringView("name_lbl",    "Select a module");
    fVersionLabel = new BStringView("version_lbl", "");
    fFileLabel    = new BStringView("file_lbl",    "");
    fStatusLabel  = new BStringView("status_lbl",  "");

    // Bold name
    BFont boldFont;
    fNameLabel->GetFont(&boldFont);
    boldFont.SetFace(B_BOLD_FACE);
    boldFont.SetSize(12.0f);
    fNameLabel->SetFont(&boldFont, B_FONT_FACE | B_FONT_SIZE);

    fToggleBtn = new BButton("toggle_btn", "Enable / Disable",
                              new BMessage(MSG_TOGGLE_MODULE));
    fReloadBtn = new BButton("reload_btn", "Reload",
                              new BMessage(MSG_RELOAD_MODULE));
    fDeleteBtn = new BButton("delete_btn", "Delete",
                              new BMessage(MSG_DELETE_MODULE));

    fToggleBtn->SetEnabled(false);
    fReloadBtn->SetEnabled(false);
    fDeleteBtn->SetEnabled(false);

    BLayoutBuilder::Group<>(fDetailsContainer, B_VERTICAL, 8)
        .SetInsets(10, 10, 10, 10)
        .Add(fNameLabel)
        .Add(fVersionLabel)
        .Add(fFileLabel)
        .Add(fStatusLabel)
        .AddGlue()
        .AddGroup(B_HORIZONTAL, 8)
            .Add(fToggleBtn)
            .Add(fReloadBtn)
            .Add(fDeleteBtn)
        .End()
        .End();

    // ── Main layout: left 40 % / right 60 % ──────────────────────────────────
    BLayoutBuilder::Group<>(this, B_HORIZONTAL, 8)
        .SetInsets(8, 8, 8, 8)
        .Add(leftPanel,         2.0f)
        .Add(fDetailsContainer, 3.0f)
        .End();
}

// ── _ReadModuleVersion ───────────────────────────────────────────────────────[...]
// Temporarily loads a .so (or .so.disabled) to read the version string.
BString ModuleManagementView::_ReadModuleVersion(const entry_ref& ref,
                                                   bool /*isActive*/)
{
    BEntry entry(&ref, true);
    BPath  p;
    if (entry.GetPath(&p) != B_OK)
        return "–";

    image_id img = load_add_on(p.Path());
    if (img < 0)
        return "–";

    BString version = "–";
    instantiate_module_func fn = nullptr;
    if (get_image_symbol(img, "instantiate_module",
                          B_SYMBOL_TYPE_TEXT, (void**)&fn) == B_OK && fn) {
        BaseModule* probe = fn();
        if (probe) {
            version = probe->Version();
            delete probe;
        }
    }
    unload_add_on(img);
    return version;
}

// ── _UpdateDetails ──────────────────────────────────────────────────────[...]
void ModuleManagementView::_UpdateDetails()
{
    int32 sel = fListView->CurrentSelection();
    if (sel < 0 || sel >= (int32)fModuleFiles.size()) {
        fNameLabel->SetText("Select a module");
        fVersionLabel->SetText("");
        fFileLabel->SetText("");
        fStatusLabel->SetText("");
        fToggleBtn->SetEnabled(false);
        fReloadBtn->SetEnabled(false);
        fDeleteBtn->SetEnabled(false);
        return;
    }

    const ModuleFileItem& item = fModuleFiles[(size_t)sel];

    fNameLabel->SetText(item.fileName.String());

    BString verText("Version: ");
    verText << item.version;
    fVersionLabel->SetText(verText.String());

    BPath p(&item.ref);
    fFileLabel->SetText(p.Path());

    if (item.isActive) {
        fStatusLabel->SetText("Status: Active");
        fToggleBtn->SetLabel("Disable");
        fToggleBtn->SetEnabled(true);
        fReloadBtn->SetEnabled(true);
    } else {
        fStatusLabel->SetText("Status: Disabled");
        fToggleBtn->SetLabel("Enable");
        fToggleBtn->SetEnabled(true);
        fReloadBtn->SetEnabled(false);
    }
    fDeleteBtn->SetEnabled(true);
}

// ── _ToggleModule ─────────────────────────────────────────────────────────[...]
void ModuleManagementView::_ToggleModule()
{
    int32 sel = fListView->CurrentSelection();
    if (sel < 0 || sel >= (int32)fModuleFiles.size())
        return;

    ModuleFileItem& item = fModuleFiles[(size_t)sel];
    BEntry entry(&item.ref);
    if (entry.InitCheck() != B_OK)
        return;

    BString oldName = item.fileName;
    BString newName = item.fileName;
    
    if (item.isActive) {
        // User wants to DISABLE: unload first, THEN rename
        fParent->DisableModuleByName(oldName.String());
        
        // Rename the file to .so.disabled
        newName << ".disabled";
    } else {
        // User wants to ENABLE: rename first, THEN load
        // Remove .disabled suffix
        if (newName.EndsWith(".so.disabled"))
            newName.Truncate(newName.Length() - 9); // Remove ".disabled" (9 chars)
    }

    status_t err = entry.Rename(newName.String());
    if (err != B_OK) {
        BString errText("Failed to rename the module file:\n");
        errText << strerror(err);
        BAlert* alert = new BAlert("Rename Error", errText.String(), "OK",
                                    nullptr, nullptr, B_WIDTH_AS_USUAL,
                                    B_STOP_ALERT);
        alert->Go();
        return;
    }

    // If enabling, load the newly renamed module
    if (!item.isActive) {
        // Update entry_ref to point to the renamed file
        BPath dir = _GetAddonsDirectory();
        BEntry newEntry(dir.Path());
        newEntry.GetParent(&newEntry);
        entry_ref newRef;
        newEntry.GetRef(&newRef);
        
        // Get the new ref for the renamed file
        BDirectory parentDir(&newEntry);
        parentDir.FindEntry(newName.String(), &newEntry);
        newEntry.GetRef(&newRef);
        
        fParent->LoadModule(newRef);
    }

    ScanModules();

    // Restore selection
    for (size_t i = 0; i < fModuleFiles.size(); ++i) {
        if (fModuleFiles[i].fileName == newName) {
            fListView->Select((int32)i);
            break;
        }
    }
}

// ── _ReloadModule ─────────────────────────────────────────────────────────[...]
void ModuleManagementView::_ReloadModule()
{
    int32 sel = fListView->CurrentSelection();
    if (sel < 0 || sel >= (int32)fModuleFiles.size())
        return;

    const ModuleFileItem& item = fModuleFiles[(size_t)sel];
    if (!item.isActive)
        return;

    BString savedName = item.fileName;
    fParent->UnloadModuleByName(savedName.String());
    fParent->LoadModule(item.ref);
    ScanModules();

    // Restore selection
    for (size_t i = 0; i < fModuleFiles.size(); ++i) {
        if (fModuleFiles[i].fileName == savedName) {
            fListView->Select((int32)i);
            break;
        }
    }
}

// ── _DeleteModule ─────────────────────────────────────────────────────────[...]
void ModuleManagementView::_DeleteModule()
{
    int32 sel = fListView->CurrentSelection();
    if (sel < 0 || sel >= (int32)fModuleFiles.size())
        return;

    const ModuleFileItem& item = fModuleFiles[(size_t)sel];

    BString msg("Are you sure you want to permanently delete '");
    msg << item.fileName << "'?";

    BAlert* alert = new BAlert("Delete Module", msg.String(),
                                "Cancel", "Delete",
                                nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    if (alert->Go() != 1)
        return;

    // If the module is active, unload it first
    if (item.isActive)
        fParent->UnloadModuleByName(item.fileName.String());

    BEntry entry(&item.ref);
    status_t err = entry.Remove();
    if (err != B_OK) {
        BString errText("Could not delete the module file:\n");
        errText << strerror(err);
        BAlert* errAlert = new BAlert("Delete Error", errText.String(), "OK",
                                       nullptr, nullptr, B_WIDTH_AS_USUAL,
                                       B_STOP_ALERT);
        errAlert->Go();
    }

    ScanModules();
}

// ── _LoadModule ─────────────────────────────────────────────────────────[...]
void ModuleManagementView::_LoadModule()
{
    if (!fFilePanel) {
        BMessenger messenger(this);
        fFilePanel = new BFilePanel(B_OPEN_PANEL, &messenger,
                                     nullptr, B_FILE_NODE, false,
                                     new BMessage(MSG_PANEL_LOAD_REF));
    }
    fFilePanel->Show();
}

// ── _GetAddonsDirectory ─────────────────────────────────────────────────────[...]
BPath ModuleManagementView::_GetAddonsDirectory()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
        path.Append("BeGin/add-ons");
    return path;
}
