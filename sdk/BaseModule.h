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
    {
    }

    virtual ~BaseModule()
    {
        delete fPropertyInfo;
    }

    // Graphical Interface (to be implemented by subclasses)
    virtual BView* GetInterfaceView() = 0;
    virtual BBitmap* GetIcon() = 0;

    // Getters for metadata
    const char* Signature() const { return fSignature.String(); }

    // Standard Scripting Integration
    virtual BHandler* ResolveSpecifier(BMessage* message, int32 index,
                                        BMessage* specifier, int32 form,
                                        const char* property) override
    {
        if (fPropertyInfo == nullptr) {
            return BHandler::ResolveSpecifier(message, index, specifier, form, property);
        }

        // Validate command using BPropertyInfo
        if (fPropertyInfo->FindMatch(message, index, specifier, form, property) >= 0) {
            return this;
        }

        return BHandler::ResolveSpecifier(message, index, specifier, form, property);
    }

    virtual void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
            case B_GET_PROPERTY:
            case B_SET_PROPERTY:
            case B_EXECUTE_PROPERTY:
            {
                // Derived classes should handle their specific properties in MessageReceived.
                // If not handled, it falls through to BHandler.
                break;
            }
        }
        BHandler::MessageReceived(message);
    }

protected:
    BString fSignature;
    BPropertyInfo* fPropertyInfo;
};

// Definition of the Factory function signature that modules must export
typedef BaseModule* (*instantiate_module_func)(void);

#endif // BEGIN_BASE_MODULE_H
