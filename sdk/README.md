# BeGin Module SDK Developer Guide

Welcome to the **BeGin** Software Development Kit (SDK) for Haiku OS. This guide provides all necessary instructions to design, code, implement, and build modular extensions (add-ons) for the BeGin container application.

Modules are compiled as standalone shared libraries (`.so` files) and loaded dynamically by the host application.

---

## 1. Class Structure and BaseModule

Every module must inherit from `BaseModule`, defined in `<BaseModule.h>` (a header-only SDK interface).

```cpp
#include <BaseModule.h>

class MyModule : public BaseModule {
public:
    MyModule();
    virtual ~MyModule();

    // Required Graphical Interface
    virtual BView* GetInterfaceView() override;
    virtual BBitmap* GetIcon() override;

    // Optional Scripting Support
    virtual BHandler* ResolveSpecifier(BMessage* message, int32 index,
                                        BMessage* specifier, int32 form,
                                        const char* property) override;
    virtual void MessageReceived(BMessage* message) override;
};
```

### Constructor Specifications
In your constructor, you must invoke the `BaseModule` constructor with two parameters:
1. **Module Name**: User-friendly name displayed on UI buttons.
2. **Signature**: A unique string identifying the module (e.g. `module/x-vnd.MyModule`).

```cpp
MyModule::MyModule()
    : BaseModule("My Module Name", "module/x-vnd.MyModule")
{
}
```

---

## 2. Implementing GUI and Icons

### Graphical View: `GetInterfaceView()`
This method must instantiate and return a `BView*` containing the module's entire interface.
* **Ownership**: Once returned, the Host takes ownership of the view, laying it out inside its central `BCardView` area.
* **Memory Safety**: Do not manually delete this view in your destructor; the Host will delete it when unloading the module.

### Module Icon: `GetIcon()`
This method must return a `BBitmap*` that represents the module's icon.
* **Icon size**: The Host draws these icons on **48x48 pixel** square sidebar buttons. We recommend returning a `BBitmap` with dimensions `16x16`, `32x32` or `48x48` in colorspace `B_RGBA32` or `B_CMAP8`.
* **Fallback**: If you return `nullptr`, a default button styled with the module name text will be used.

---

## 3. Global Scripting Support (Integrating with `hey`)

Haiku OS supports a system-wide scripting protocol. Users can interact with your module from the Terminal using the `hey` tool:
```bash
hey BeGin Module "My Module Name" GET Status
```

To enable scripting:
1. Initialize a `BPropertyInfo` instance in your module's constructor.
2. Define a static array of `property_info` structures defining properties you wish to expose.

```cpp
static property_info sPropInfo[] = {
    {
        "Status",
        { B_GET_PROPERTY, 0 },
        { B_DIRECT_SPECIFIER, 0 },
        "Get current module operational status.",
        0
    },
    { 0 }
};

MyModule::MyModule()
    : BaseModule("My Module Name", "module/x-vnd.MyModule")
{
    fPropertyInfo = new BPropertyInfo(sPropInfo);
}
```

### Resolving Specifiers
In `ResolveSpecifier()`, match incoming properties against your property info table:

```cpp
BHandler* MyModule::ResolveSpecifier(BMessage* message, int32 index,
                                     BMessage* specifier, int32 form,
                                     const char* property)
{
    if (fPropertyInfo->FindMatch(message, index, specifier, form, property) >= 0) {
        return this; // We handle this property
    }
    return BaseModule::ResolveSpecifier(message, index, specifier, form, property);
}
```

### Handling Messages
Handle scripting commands inside `MessageReceived()` by intercepting the `B_GET_PROPERTY`, `B_SET_PROPERTY` or `B_EXECUTE_PROPERTY` messages:

```cpp
void MyModule::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case B_GET_PROPERTY:
        {
            BMessage reply(B_REPLY);
            int32 index;
            BMessage specifier;
            int32 form;
            const char* property = nullptr;
            
            if (message->GetCurrentSpecifier(&index, &specifier, &form, &property) == B_OK) {
                if (property != nullptr && strcmp(property, "Status") == 0) {
                    reply.AddString("result", "Running normally");
                    message->SendReply(&reply);
                    return;
                }
            }
            break;
        }
    }
    BaseModule::MessageReceived(message);
}
```

---

## 4. Exporting the Factory Symbol

For the Host to load your module, you **MUST** export the global factory function named `instantiate_module`. Declare it using `extern "C"` to prevent C++ name mangling:

```cpp
extern "C" BaseModule* instantiate_module() {
    return new MyModule();
}
```

---

## 5. Memory Safety & Stability Guardrails

To prevent the Host container from crashing, modules must adhere to these rules:
1. **Never block the UI Thread**: Do not run CPU-intensive tasks, infinite loops, or network calls directly inside event methods (like `MessageReceived`, `Draw` or layout updates). If you do, the Host's **Watchdog** will detect that the main UI thread has blocked for **more than 1 second** and will immediately **force-disable and unload** your module.
2. **Handle C++ Exceptions**: Standard exceptions thrown within module handlers are trapped by the Host. An uncaught exception will cause the Host to deactivate your module and display a red warning banner.
3. **Smart Pointers**: Use `std::unique_ptr` or `std::shared_ptr` to manage internal module objects.
4. **Thread Safety**: If you spawn worker threads, make sure to call `Window()->LockLooper()` and `Window()->UnlockLooper()` before modifying any graphical views to prevent concurrency crashes.

---

## 6. How to Build Your Module Standalone

You do not need to compile or source the main BeGin Host application to build your module. You only need the `sdk/BaseModule.h` header file.

Here is a template `CMakeLists.txt` to compile your module:

```cmake
cmake_minimum_required(VERSION 3.12)
project(MyModule LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile as a shared module (add-on library)
add_library(MyModule MODULE MyModule.cpp)

# Include the BeGin SDK path
target_include_directories(MyModule PRIVATE /path/to/BeGin/sdk)

# Find and link Haiku native system libraries (libbe.so)
find_library(BE_LIBRARY be REQUIRED)
target_link_libraries(MyModule PRIVATE ${BE_LIBRARY})

# Optional: strip debug symbols to keep the binary small
set_target_properties(MyModule PROPERTIES PREFIX "")
```

### Installation
Move the compiled binary output (e.g. `MyModule.so`) to:
`/boot/home/config/settings/BeGin/add-ons/`

The Host application will automatically detect (via its Node Monitor in future revisions, or manual import) and load the module.
