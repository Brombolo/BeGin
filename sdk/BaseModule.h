// BaseModule.h – BeGin SDK: base class that all add-on modules must inherit from
#ifndef BEGIN_BASE_MODULE_H
#define BEGIN_BASE_MODULE_H

#include <Handler.h>
#include <View.h>
#include <Bitmap.h>
#include <PropertyInfo.h>
#include <Message.h>
#include <String.h>

class BaseModule : public BHandler {
public:
    BaseModule(const char* name, const char* signature)
        : BHandler(name),
          fSignature(signature),
          fPropertyInfo(nullptr)
    {}

    virtual ~BaseModule()
    {
        delete fPropertyInfo;
    }

    // ── Interface to implement in subclasses ──────────────────────────────────
    virtual BView*   GetInterfaceView() = 0; // Return the module's content view
    virtual BBitmap* GetIcon()          = 0; // Return the module's sidebar icon
    virtual const char* Version() const { return "1.0.0"; } // Module version

    // ── Metadata accessors ────────────────────────────────────────────────────
    const char* Signature() const { return fSignature.String(); }

    // ── BHandler scripting integration ───────────────────────────────────────
    virtual BHandler* ResolveSpecifier(BMessage* message, int32 index,
                                        BMessage* specifier, int32 form,
                                        const char* property) override
    {
        if (fPropertyInfo == nullptr)
            return BHandler::ResolveSpecifier(message, index, specifier,
                                               form, property);

        if (fPropertyInfo->FindMatch(message, index, specifier,
                                      form, property) >= 0)
            return this;

        return BHandler::ResolveSpecifier(message, index, specifier,
                                           form, property);
    }

    virtual void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
            case B_GET_PROPERTY:
            case B_SET_PROPERTY:
            case B_EXECUTE_PROPERTY:
                // Subclasses should handle their specific properties;
                // unhandled ones fall through to BHandler.
                break;
        }
        BHandler::MessageReceived(message);
    }

protected:
    BString        fSignature;
    BPropertyInfo* fPropertyInfo;
};

// Factory function signature that every module shared library must export
typedef BaseModule* (*instantiate_module_func)(void);

#endif // BEGIN_BASE_MODULE_H
