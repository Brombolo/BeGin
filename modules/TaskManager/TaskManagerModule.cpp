// TaskManagerModule.cpp – System process monitor add-on module
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
#include <stdio.h>

// ── Internal message codes ────────────────────────────────────────────────────
enum {
    MSG_REFRESH_TICK       = 'rftk', // Periodic auto-refresh timer
    MSG_REFRESH_CMD        = 'rfcm', // Manual refresh button
    MSG_SIMULATE_EXCEPTION = 'smex', // Test: throw a C++ exception
    MSG_SIMULATE_HANG      = 'smhg', // Test: enter an infinite loop
};

// ── Scripting properties ──────────────────────────────────────────────────────
static property_info sPropInfo[] = {
    {
        "ProcessCount",
        { B_GET_PROPERTY, 0 },
        { B_DIRECT_SPECIFIER, 0 },
        "Returns the number of currently running teams (processes).",
        0
    },
    { 0 }
};

// ── TaskManagerView – the GUI widget ─────────────────────────────────────────
class TaskManagerView : public BView {
public:
    TaskManagerView(BHandler* targetModule)
        : BView("TaskManagerView", B_WILL_DRAW),
          fTargetModule(targetModule)
    {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        fListView  = new BListView("process_list");
        fScrollView = new BScrollView("scroll_list", fListView, 0, true, true);

        // Standard controls
        fRefreshBtn = new BButton("Refresh", new BMessage(MSG_REFRESH_CMD));
        fRefreshBtn->SetTarget(fTargetModule);

        // Fault simulation controls (for testing the host watchdog)
        fExceptionBtn = new BButton("Simulate Exception",
                                     new BMessage(MSG_SIMULATE_EXCEPTION));
        fExceptionBtn->SetTarget(fTargetModule);

        fHangBtn = new BButton("Simulate Hang",
                                new BMessage(MSG_SIMULATE_HANG));
        fHangBtn->SetTarget(fTargetModule);

        BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
            .SetInsets(10, 10, 10, 10)
            .Add(fScrollView, 1.0f)
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

// ── TaskManager – the module class ───────────────────────────────────────────
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

        // Generate a simple 16×16 green icon
        fIcon = new BBitmap(BRect(0, 0, 15, 15), B_RGBA32);
        uint32* bits = (uint32*)fIcon->Bits();
        if (bits) {
            for (int i = 0; i < 256; ++i)
                bits[i] = 0xFF00DD00;
        }
    }

    virtual ~TaskManager()
    {
        delete fRunner;
        delete fIcon;
        // fView lifetime is managed by the host MainWindow
    }

    virtual BView* GetInterfaceView() override
    {
        if (!fView) {
            fView = new TaskManagerView(this);
            _RefreshProcessList();
            // Auto-refresh every 2 seconds
            fRunner = new BMessageRunner(BMessenger(this),
                                          new BMessage(MSG_REFRESH_TICK),
                                          2000000);
        }
        return fView;
    }

    virtual BBitmap* GetIcon() override { return fIcon; }

    virtual const char* Version() const override { return "1.0.0"; }

    virtual void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
            case MSG_REFRESH_TICK:
            case MSG_REFRESH_CMD:
                _RefreshProcessList();
                break;

            case MSG_SIMULATE_EXCEPTION:
                // Triggers the C++ exception handler in MainWindow::DispatchMessage
                throw std::runtime_error(
                    "Simulated fault: unhandled C++ exception from module.");

            case MSG_SIMULATE_HANG:
                // Triggers the watchdog timeout in MainWindow::DispatchMessage
                {
                    volatile bool keepHanging = true;
                    while (keepHanging) {
                        // Infinite loop — blocks the UI thread intentionally
                    }
                }
                break;

            case B_GET_PROPERTY:
            {
                BMessage reply(B_REPLY);
                int32       index;
                BMessage    specifier;
                int32       form;
                const char* property = nullptr;

                if (message->GetCurrentSpecifier(&index, &specifier,
                                                  &form, &property) == B_OK) {
                    if (property && strcmp(property, "ProcessCount") == 0) {
                        int32     count  = 0;
                        int32     cookie = 0;
                        team_info info;
                        while (get_next_team_info(&cookie, &info) == B_OK)
                            count++;
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
        if (fPropertyInfo->FindMatch(message, index, specifier,
                                      form, property) >= 0)
            return this;
        return BaseModule::ResolveSpecifier(message, index, specifier,
                                             form, property);
    }

private:
    // Returns the total CPU time (user + kernel) across all threads of a team
    bigtime_t _GetTeamCPUTime(team_id team)
    {
        bigtime_t  cpuTime = 0;
        int32      cookie  = 0;
        thread_info th;
        while (get_next_thread_info(team, &cookie, &th) == B_OK)
            cpuTime += th.user_time + th.kernel_time;
        return cpuTime;
    }

    // Returns the total physical RAM usage of a team across all its mapped areas
    size_t _GetTeamMemory(team_id team)
    {
        size_t   memory    = 0;
        ssize_t  areaCookie = 0;
        area_info arInfo;
        while (get_next_area_info(team, &areaCookie, &arInfo) == B_OK)
            memory += arInfo.ram_size;
        return memory;
    }

    void _RefreshProcessList()
    {
        if (!fView || !fView->GetListView())
            return;

        BListView* list = fView->GetListView();

        if (!LockLooper())
            return;

        // Clear previous entries
        int32 count = list->CountItems();
        for (int32 i = count - 1; i >= 0; --i)
            delete list->RemoveItem(i);

        // Snapshot system-wide timing and memory stats
        bigtime_t now      = system_time();
        bigtime_t deltaReal = now - fLastSampleTime;
        fLastSampleTime    = now;

        system_info sysInfo;
        get_system_info(&sysInfo);
        int32  cpuCount       = sysInfo.cpu_count > 0 ? sysInfo.cpu_count : 1;
        double totalMemBytes  = (double)sysInfo.max_pages * B_PAGE_SIZE;

        std::map<team_id, bigtime_t> currentCPUTimes;

        int32     cookie = 0;
        team_info info;
        while (get_next_team_info(&cookie, &info) == B_OK) {
            // ── CPU % ───────────────────────────────────────────────────────
            bigtime_t curCPU = _GetTeamCPUTime(info.team);
            currentCPUTimes[info.team] = curCPU;

            double cpuPct = 0.0;
            if (deltaReal > 0 && fLastCPUTimes.count(info.team) > 0) {
                bigtime_t deltaCPU = curCPU - fLastCPUTimes[info.team];
                cpuPct = ((double)deltaCPU / deltaReal * 100.0) / cpuCount;
                if (cpuPct < 0.0)   cpuPct = 0.0;
                if (cpuPct > 100.0) cpuPct = 100.0;
            }

            // ── RAM % ───────────────────────────────────────────────────────
            size_t teamMem = _GetTeamMemory(info.team);
            double memPct  = (totalMemBytes > 0)
                             ? ((double)teamMem / totalMemBytes * 100.0) : 0.0;
            if (memPct < 0.0)   memPct = 0.0;
            if (memPct > 100.0) memPct = 100.0;

            // ── Format and add to list ────────────────────────────────────
            char buf[256];
            sprintf(buf, "%s  (PID: %d  CPU: %.1f%%  RAM: %.1f%%)",
                    info.name, (int)info.team, cpuPct, memPct);
            list->AddItem(new BStringItem(buf));
        }

        fLastCPUTimes = currentCPUTimes;
        UnlockLooper();
    }

    TaskManagerView*             fView;
    BMessageRunner*              fRunner;
    BBitmap*                     fIcon;
    std::map<team_id, bigtime_t> fLastCPUTimes;
    bigtime_t                    fLastSampleTime;
};

// ── Factory function – must be exported with C linkage ────────────────────────
extern "C" BaseModule* instantiate_module()
{
    return new TaskManager();
}
