#include "App.h"
#include "MainWindow.h"

BeGinApp::BeGinApp()
    : BApplication("application/x-vnd.BeGin")
{
}

BeGinApp::~BeGinApp()
{
}

void BeGinApp::ReadyToRun()
{
    MainWindow* window = new MainWindow();
    window->Show();
}
