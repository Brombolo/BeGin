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
#include <image.h>
#include <vector>

#include "BaseModule.h"
#include "EmptyView.h"

// Message constants
enum {
    MSG_FILE_EXIT = 'fext',
    MSG_LOAD_MODULE_PROMPT = 'ldmp',
    MSG_SELECT_MODULE = 'slmd'
};

struct LoadedModule {
    image_id image;
    BaseModule* instance;
    BButton* button;
    BView* view;
    int32 cardIndex;
};

class MainWindow : public BWindow {
public:
    MainWindow();
    virtual ~MainWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

private:
    void _InitInterface();
    void _InitModulesDirectory();
    void _LoadModule(const entry_ref& ref);
    void _AddModuleToUI(LoadedModule& loaded);
    void _SelectModule(const BString& signature);
    BPath _GetModulesDirectory();

    BMenuBar*           fMenuBar;
    BGroupView*         fSidebarView;
    BCardView*          fCardView;
    EmptyView*          fEmptyView;
    BFilePanel*         fFilePanel;

    std::vector<LoadedModule> fModules;
};

#endif // MAIN_WINDOW_H
