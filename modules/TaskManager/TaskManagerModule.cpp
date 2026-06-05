#include <BaseModule.h>

#include <View.h>
#include <ListView.h>
#include <ScrollView.h>
#include <Button.h>
#include <StringItem.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Alert.h>
#include <OS.h>

#include <stdexcept>
#include <vector>
#include <map>

// Messages
enum {
    MSG_REFRESH_TICK = 'rftk',
    MSG_REFRESH_CMD = 'rfcm',
    MSG_SIMULATE_EXCEPTION = 'smex',
    MSG_SIMULATE_HANG = 'smhg'
};

// Scripting Properties Table
static property_info sPropInfo[] = {
    {
        "ProcessCount",
        { B_GET_PROPERTY, 0 },
        { B_DIRECT_SPECIFIER, 0 },
        "Get the number of active running processes (teams).",
        0
    },
    { 0 }
};

// Task Manager GUI View Class
class TaskManagerView : public BView {
public:
    TaskManagerView(BHandler* targetModule)
        : BView("TaskManagerView", B_WILL_DRAW),
          fTargetModule(targetModule)
    {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        fListView = new BListView("process_list");
        fScrollView = new BScrollView("scroll_list", fListView, 0, true, true);

        // Standard Actions
        fRefreshBtn = new BButton("Aggiorna", new BMessage(MSG_REFRESH_CMD));
        fRefreshBtn->SetTarget(fTargetModule);

        // Crash Simulation Actions
        fExceptionBtn = new BButton("Simula Eccezione", new BMessage(MSG_SIMULATE_EXCEPTION));
        fExceptionBtn->SetTarget(fTargetModule);

        fHangBtn = new BButton("Simula Blocco", new BMessage(MSG_SIMULATE_HANG));
        fHangBtn->SetTarget(fTargetModule);

        // Lay out UI
        BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
            .SetInsets(10, 10, 10, 10)
            .Add(fScrollView, 1.0f) // list takes all remaining vertical space
            .AddGroup(B_HORIZONTAL, 10)
                .Add(fRefreshBtn)
                .AddGlue()
                .Add(fExceptionBtn)
                .Add(fHangBtn)
            .End()
        .End();
    }

    virtual ~TaskManagerView() {}

    BListView* GetListView() { return fListView; }

private:
    BHandler*    fTargetModule;
    BListView*   fListView;
    BScrollView* fScrollView;
    BButton*     fRefreshBtn;
    BButton*     fExceptionBtn;
    BButton*     fHangBtn;
};

// Task Manager SDK Module Class
class TaskManager : public BaseModule {
public:
    TaskManager()
        : BaseModule("Task Manager", "module/x-vnd.TaskManager"),
          fView(nullptr),
          fRunner(nullptr),
          fIcon(nullptr),
          fLastSampleTime(0)
    {
        fPropertyInfo = new BPropertyInfo(sPropInfo);
        fLastSampleTime = system_time();
        
        // Generate a simple green 16x16 programmatical icon to verify SetIcon
        fIcon = new BBitmap(BRect(0, 0, 15, 15), B_RGBA32);
        uint32* bits = (uint32*)fIcon->Bits();
        if (bits != nullptr) {
            for (int i = 0; i < 256; ++i) {
                bits[i] = 0xFF00DD00; // Green with slight alpha/color shift
            }
        }
    }

    virtual ~TaskManager()
    {
        delete fRunner;
        delete fIcon;
        // fView is deleted by the Host MainWindow
    }

    virtual BView* GetInterfaceView() override
    {
        if (fView == nullptr) {
            fView = new TaskManagerView(this);
            // Refresh initially
            _RefreshProcessList();
            
            // Start dynamic periodic refreshing (every 2 seconds)
            fRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_REFRESH_TICK), 2000000);
        }
        return fView;
    }

    virtual BBitmap* GetIcon() override
    {
        return fIcon;
    }

    virtual void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
            case MSG_REFRESH_TICK:
            case MSG_REFRESH_CMD:
                _RefreshProcessList();
                break;

            case MSG_SIMULATE_EXCEPTION:
                // Triggers exception handling inside MainWindow's DispatchMessage
                throw std::runtime_error("Malfunzionamento simulato: Eccezione C++ non gestita nel modulo.");
                break;

            case MSG_SIMULATE_HANG:
                // Triggers Watchdog timeout detection in MainWindow's DispatchMessage (>1s block)
                while (true) {
                    // Infinite Loop to freeze UI thread
                }
                break;

            case B_GET_PROPERTY:
            {
                BMessage reply(B_REPLY);
                int32 index;
                BMessage specifier;
                int32 form;
                const char* property = nullptr;
                
                if (message->GetCurrentSpecifier(&index, &specifier, &form, &property) == B_OK) {
                    if (property != nullptr && strcmp(property, "ProcessCount") == 0) {
                        int32 count = 0;
                        int32 cookie = 0;
                        team_info info;
                        while (get_next_team_info(&cookie, &info) == B_OK) {
                            count++;
                        }
                        reply.AddInt32("result", count);
                        message->SendReply(&reply);
                        return;
                    }
                }
                break;
            }

            default:
                BaseModule::MessageReceived(message);
                break;
        }
    }

    virtual BHandler* ResolveSpecifier(BMessage* message, int32 index,
                                        BMessage* specifier, int32 form,
                                        const char* property) override
    {
        if (fPropertyInfo->FindMatch(message, index, specifier, form, property) >= 0) {
            return this;
        }
        return BaseModule::ResolveSpecifier(message, index, specifier, form, property);
    }

private:
    bigtime_t _GetTeamCPUTime(team_id team)
    {
        bigtime_t cpuTime = 0;
        int32 threadCookie = 0;
        thread_info thInfo;
        while (get_next_thread_info(team, &threadCookie, &thInfo) == B_OK) {
            cpuTime += thInfo.user_time + thInfo.kernel_time;
        }
        return cpuTime;
    }

    size_t _GetTeamMemory(team_id team)
    {
        size_t memory = 0;
        int32 areaCookie = 0;
        area_info arInfo;
        while (get_next_area_info(team, &areaCookie, &arInfo) == B_OK) {
            memory += arInfo.ram_size;
        }
        return memory;
    }

    void _RefreshProcessList()
    {
        if (fView == nullptr || fView->GetListView() == nullptr) return;

        BListView* list = fView->GetListView();
        
        // Ensure looper is locked during view manipulation
        if (LockLooper()) {
            // Memory-safe clearing of BListView
            int32 count = list->CountItems();
            for (int32 i = count - 1; i >= 0; --i) {
                BListItem* item = list->RemoveItem(i);
                delete item;
            }

            // Retrieve CPU and RAM system-wide info
            bigtime_t now = system_time();
            bigtime_t deltaReal = now - fLastSampleTime;
            fLastSampleTime = now;

            system_info sysInfo;
            get_system_info(&sysInfo);
            int32 cpuCount = sysInfo.cpu_count;
            if (cpuCount < 1) cpuCount = 1;
            double totalMemoryBytes = (double)sysInfo.max_pages * B_PAGE_SIZE;

            std::map<team_id, bigtime_t> currentCPUTimes;

            // Retrieve all running teams (processes)
            int32 cookie = 0;
            team_info info;
            while (get_next_team_info(&cookie, &info) == B_OK) {
                // 1. Calculate CPU usage percentage
                bigtime_t currentCPU = _GetTeamCPUTime(info.team);
                currentCPUTimes[info.team] = currentCPU;

                double cpuPercent = 0.0;
                if (deltaReal > 0 && fLastCPUTimes.count(info.team) > 0) {
                    bigtime_t deltaCPU = currentCPU - fLastCPUTimes[info.team];
                    cpuPercent = ((double)deltaCPU / deltaReal * 100.0) / cpuCount;
                    if (cpuPercent < 0.0) cpuPercent = 0.0;
                    if (cpuPercent > 100.0) cpuPercent = 100.0;
                }

                // 2. Calculate RAM usage percentage
                size_t teamMem = _GetTeamMemory(info.team);
                double memPercent = (totalMemoryBytes > 0) ? ((double)teamMem / totalMemoryBytes * 100.0) : 0.0;
                if (memPercent < 0.0) memPercent = 0.0;
                if (memPercent > 100.0) memPercent = 100.0;

                // 3. Format the display string
                BString itemText;
                itemText.Format("%s (PID: %d, CPU: %.1f%%, RAM: %.1f%%)",
                                info.name, (int)info.team, cpuPercent, memPercent);
                list->AddItem(new BStringItem(itemText.String()));
            }

            fLastCPUTimes = currentCPUTimes;

            UnlockLooper();
        }
    }

    TaskManagerView*             fView;
    BMessageRunner*              fRunner;
    BBitmap*                     fIcon;
    std::map<team_id, bigtime_t> fLastCPUTimes;
    bigtime_t                    fLastSampleTime;
};

// Export factory function with extern "C" to prevent name mangling
extern "C" BaseModule* instantiate_module()
{
    return new TaskManager();
}
