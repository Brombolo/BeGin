#ifndef APP_H
#define APP_H

#include <Application.h>

class BeGinApp : public BApplication {
public:
    BeGinApp();
    virtual ~BeGinApp();

    virtual void ReadyToRun() override;
};

#endif // APP_H
