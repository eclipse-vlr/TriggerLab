namespace {
struct ClickTestTarget {
    COLORREF color=RGB(231,67,19);
    int downs=0,ups=0;
    bool crosshair=false;
    std::vector<triggerlab::Clock::time_point> clicks;
    std::vector<triggerlab::Clock::time_point> releases;
};
LRESULT CALLBACK clickTestProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* state=reinterpret_cast<ClickTestTarget*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(state && message==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rect{};GetClientRect(hwnd,&rect);
        SetDCBrushColor(dc,state->color);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(state->crosshair) {RECT cross{rect.right/2-3,rect.bottom/2-3,rect.right/2+4,rect.bottom/2+4};FillRect(dc,&cross,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));}
        EndPaint(hwnd,&ps);return 0;
    }
    if(state && message==WM_LBUTTONDOWN) {++state->downs;state->clicks.push_back(triggerlab::Clock::now());return 0;}
    if(state && message==WM_LBUTTONUP) {++state->ups;state->releases.push_back(triggerlab::Clock::now());return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
// Real capture and pulse scheduling, inert input sender: no desktop clicks.
int rapidTargetTest(Preview& app) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::ofstream report(std::filesystem::path(executable).parent_path()/L"target-sequence-test-report.txt");
    report<<std::unitbuf;
    POINT cursor{};
    if(!GetCursorPos(&cursor) || Preview::inputHeld()) {report<<"SKIP: physical input held\n";return 77;}
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor)) return 77;
    // The real cursor must stay in the selected window for the input guard.
    // Keep the analyzed center away from it without moving the user's pointer.
    constexpr int sceneSide=640,nearEdge=30,farEdge=sceneSide-nearEdge;
    if(monitor.rcMonitor.right-monitor.rcMonitor.left<sceneSide || monitor.rcMonitor.bottom-monitor.rcMonitor.top<sceneSide) return 77;
    const int x=cursor.x+farEdge>monitor.rcMonitor.right?cursor.x-farEdge:cursor.x-nearEdge;
    const int y=cursor.y+farEdge>monitor.rcMonitor.bottom?cursor.y-farEdge:cursor.y-nearEdge;
    ClickTestTarget state;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=clickTestProcedure;wc.lpszClassName=L"TriggerLabTargetSequenceTest";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    const HWND previous=GetForegroundWindow();
    HWND target=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab target sequence test",WS_POPUP|WS_VISIBLE,x,y,sceneSide,sceneSide,nullptr,nullptr,wc.hInstance,&state);
    if(!target) return 77;
    struct Cleanup {
        Preview& app;HWND target,previous;
        ~Cleanup() {
            const bool restore=GetForegroundWindow()==target;
            app.stop();app.targetWindow=nullptr;DestroyWindow(target);
            if(restore && previous && IsWindow(previous)) SetForegroundWindow(previous);
        }
    } cleanup{app,target,previous};
    auto fail=[&](const char* reason) {
        std::cerr<<"FAIL: target sequence: "<<reason<<'\n';
        report<<"FAIL: "<<reason<<" downs="<<recoveryDowns<<" ups="<<recoveryUps
            <<" running="<<app.running<<" armed="<<app.autoClick<<" dry="<<app.settings.dryRun
            <<" fresh="<<app.sample.frame.has_value()<<" found="<<app.mask.found<<" pending="<<app.syntheticDown
            <<" latch="<<app.gate.clickedHover()<<" dirty="<<app.settingsDirty()<<" colorDirty="<<app.colorDirty()<<'\n';
        const int bytes=WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),nullptr,0,nullptr,nullptr);
        std::string status(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),status.data(),bytes,nullptr,nullptr);
        report<<status<<'\n';return 2;
    };
    const auto foregroundThread=GetWindowThreadProcessId(previous,nullptr),thread=GetCurrentThreadId();
    const bool attached=foregroundThread && foregroundThread!=thread && AttachThreadInput(thread,foregroundThread,TRUE);
    SetForegroundWindow(target);SetActiveWindow(target);SetFocus(target);
    if(attached) AttachThreadInput(thread,foregroundThread,FALSE);
    UpdateWindow(target);pumpTestMessages();
    if(GetForegroundWindow()!=target) {report<<"SKIP: target focus unavailable\n";return 77;}
    app.settings={};app.settings.ignoreRadius=0;app.settings.dwellMs=1;app.settings.clickMs=150;
    app.settings.dryRun=false;app.color.target={19,67,231,255};app.color.channelTolerance=0;
    app.targetWindow=target;GetWindowThreadProcessId(target,&app.targetProcess);
    app.syncSettings();app.syncColor();if(!app.saveAll()) return fail("isolated settings save");
    const auto bounds=app.clientBounds();const auto roi=bounds?triggerlab::aimRegion(*bounds,app.settings):std::optional<triggerlab::Region>{};
    if(!roi || !app.permittedRegion(*roi)) {report<<"SKIP: test ROI covered or unavailable\n";return 77;}
    app.verified=true;app.running=true;app.autoClick=true;
    recoveryDowns=0;recoveryUps=0;rejectRecoveryUp=false;
    auto paint=[&](COLORREF color) {state.color=color;InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);GdiFlush();DwmFlush();};
    bool stable=true;
    auto waitFor=[&](auto predicate) {
        const auto deadline=triggerlab::Clock::now()+1500ms;
        while(!predicate() && triggerlab::Clock::now()<deadline) {
            POINT current{};
            if(!GetCursorPos(&current) || current.x!=cursor.x || current.y!=cursor.y || GetForegroundWindow()!=target || Preview::inputHeld() || !app.permittedRegion(*roi)) {stable=false;return false;}
            const auto sampleStarted=triggerlab::Clock::now();app.update();
            report<<"sample_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(triggerlab::Clock::now()-sampleStarted).count()
                <<" dwell_ms="<<app.gate.elapsed(triggerlab::Clock::now()).count()<<'\n';
            pumpTestMessages();Sleep(1);
        }
        return predicate();
    };
    for(int count=1;count<=6;++count) {
        paint(RGB(231,67,19));
        if(!waitFor([&] {return recoveryDowns.load()>=count;})) {
            if(!stable) {report<<"SKIP: foreground or physical input changed\n";return 77;}
            return fail("next target did not trigger without cursor movement");
        }
        if(recoveryDowns!=count || !app.syntheticDown) return fail("unexpected pulse count");
        paint(RGB(50,50,50));app.update();
        if(!app.sample.frame || app.mask.found) return fail("target departure during pulse was not captured");
        if(app.gate.clickedHover()) return fail("target departure during pulse did not rearm");
        if(count<6) paint(RGB(231,67,19));
        if(!waitFor([&] {return app.clickCount>=static_cast<std::uint64_t>(count);})) return fail("pulse did not finish");
    }
    if(recoveryDowns!=6 || recoveryUps!=6 || app.syntheticDown || !app.autoClick) return fail("unbalanced sequence");
    // Sender above never forwards input to Windows or the test window.
    if(state.downs || state.ups) return fail("inert sequence sent real input");
    report<<"PASS: six targets, loss during 150ms pulse, no cursor movement, balanced fake input\n";
    return 0;
}
int referenceCaptureTest(Preview& app) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::ofstream report(std::filesystem::path(executable).parent_path()/L"reference-test-report.txt");
    POINT cursor{};
    if(!GetCursorPos(&cursor) || Preview::inputHeld()) {report<<"SKIP: physical input held or cursor unavailable\n";return 77;}
    const HWND previous=GetForegroundWindow();
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor)) {report<<"SKIP: monitor unavailable\n";return 77;}
    const RECT work=monitor.rcWork;
    if(work.right-work.left<300 || work.bottom-work.top<300) {report<<"SKIP: desktop too small\n";return 77;}
    const int x=cursor.x<(work.left+work.right)/2?work.right-260:work.left+20;
    const int y=cursor.y<(work.top+work.bottom)/2?work.bottom-260:work.top+20;
    ClickTestTarget state;state.crosshair=true;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=clickTestProcedure;wc.lpszClassName=L"TriggerLabReferenceCaptureTest";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    HWND target=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab reference capture test",WS_POPUP|WS_VISIBLE,x,y,240,240,nullptr,nullptr,wc.hInstance,&state);
    if(!target) {report<<"SKIP: test window unavailable\n";return 77;}
    struct Cleanup {
        Preview& app;HWND target,previous,blocker=nullptr;
        ~Cleanup() {
            const HWND foreground=GetForegroundWindow();const bool restore=foreground==target || foreground==blocker || foreground==app.window;
            app.stop();app.clearReferences();app.targetWindow=nullptr;app.targetProcess=0;
            if(blocker) DestroyWindow(blocker);if(target) DestroyWindow(target);
            if(restore && previous && IsWindow(previous)) SetForegroundWindow(previous);
        }
    } cleanup{app,target,previous};
    auto fail=[&](const char* reason) {report<<"FAIL: "<<reason<<'\n';std::cerr<<"FAIL: "<<reason<<'\n';return 2;};
    auto skip=[&](const char* reason) {report<<"SKIP: "<<reason<<'\n';std::cout<<"SKIP: "<<reason<<'\n';return 77;};
    auto pump=[] {MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}};
    auto activate=[&] {
        const DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),thread=GetCurrentThreadId();
        const bool attached=foregroundThread && foregroundThread!=thread && AttachThreadInput(thread,foregroundThread,TRUE);
        SetForegroundWindow(target);SetActiveWindow(target);SetFocus(target);
        if(attached) AttachThreadInput(thread,foregroundThread,FALSE);
        UpdateWindow(target);GdiFlush();DwmFlush();Sleep(40);pump();
        return GetForegroundWindow()==target;
    };
    app.settings={};app.color.target={19,67,231,255};app.color.channelTolerance=0;app.color.space=triggerlab::ColorSpace::RGB;
    app.syncSettings();app.syncColor();if(!app.saveAll()) return fail("isolated settings could not be saved");
    app.targetWindow=target;GetWindowThreadProcessId(target,&app.targetProcess);app.clearReferences();
    if(!activate()) return skip("test window cannot receive focus");
    const auto bounds=app.clientBounds();const auto roi=bounds?triggerlab::aimRegion(*bounds,app.settings):std::optional<triggerlab::Region>{};
    if(!roi) return fail("owned test ROI invalid");
    auto probe=app.capture.capture(*roi);
    if(!probe.frame || triggerlab::aimColorMask(*probe.frame,app.color,app.settings).coverage!=1) return skip("reference pixels unavailable or covered");
    // A display-only image must survive without becoming a fresh input sample.
    app.displayFrame.emplace(1,1);app.displayFrame->at(0,0)={7,8,9,255};
    SetWindowPos(app.window,nullptr,work.left,work.top,480,360,SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(app.window,SW_SHOWNOACTIVATE);
    auto start=[&](int button) {
        SetForegroundWindow(app.window);SetActiveWindow(app.window);SetFocus(app.window);
        app.command(button);
        return app.aimPickDeadline!=0;
    };
    auto waitForSnapshot=[&] {
        const auto deadline=triggerlab::Clock::now()+4s;
        while(app.aimPickDeadline && triggerlab::Clock::now()<deadline) {
            POINT current{};
            if(GetForegroundWindow()!=target) {report<<"foreground_changed=1\n";return 77;}
            // References sample the client center, so pointer movement outside
            // the ROI is harmless. A pointer overlay over the ROI is not.
            if(!GetCursorPos(&current) || Preview::inputHeld() || (std::abs(current.x-(roi->x+roi->width/2))<80 && std::abs(current.y-(roi->y+roi->height/2))<80)) {report<<"physical_input_or_pointer_overlay=1\n";return 77;}
            pump();Sleep(10);
        }
        return app.aimPickDeadline?2:0;
    };
    const auto countdownStart=GetTickCount64();
    if(!start(31)) return app.status==L"F9 занят: снимок не начат"?skip("F9 unavailable"):fail("target snapshot did not start");
    const ULONGLONG firstDeadline=app.aimPickDeadline;
    if(app.snapshotPurpose!=Preview::SnapshotPurpose::Target || firstDeadline<countdownStart+3000 || firstDeadline>GetTickCount64()+3000 || app.autoClick) return fail("target countdown or input state");
    if(GetForegroundWindow()!=target) return fail("snapshot did not automatically activate the selected window");
    const auto firstStart=triggerlab::Clock::now();const int firstWait=waitForSnapshot();
    if(firstWait) return firstWait==77?skip("input or foreground changed during target capture"):fail("target timer did not finish");
    if(GetTickCount64()<firstDeadline) return fail("target captured before countdown deadline");
    report<<"target_wait_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(triggerlab::Clock::now()-firstStart).count()<<'\n';
    if(!app.targetReference || app.backgroundReference || app.targetReference->at(0,0).red!=231) return fail("fresh target reference not recorded");
    const auto retained=*app.targetReference;
    state.color=RGB(0,255,0);InvalidateRect(target,nullptr,FALSE);
    if(!activate()) return skip("background focus unavailable");
    probe=app.capture.capture(*roi);
    if(!probe.frame || triggerlab::aimColorMask(*probe.frame,app.color,app.settings).coverage!=0) return skip("background pixels unavailable or covered");
    if(!start(32)) return fail("background snapshot did not start");
    const auto secondDeadline=app.aimPickDeadline;
    if(app.snapshotPurpose!=Preview::SnapshotPurpose::Background) return fail("background countdown purpose incorrect");
    if(GetForegroundWindow()!=target) return fail("background snapshot did not automatically activate the selected window");
    const int secondWait=waitForSnapshot();
    if(secondWait) return secondWait==77?skip("input or foreground changed during background capture"):fail("background timer did not finish");
    if(GetTickCount64()<secondDeadline) return fail("background captured before countdown deadline");
    const auto quality=triggerlab::compareColorSamples(app.targetReference?&*app.targetReference:nullptr,app.backgroundReference?&*app.backgroundReference:nullptr,app.color,app.settings);
    if(!quality.valid || !quality.targetFound || quality.backgroundFound || quality.targetCoverage!=1.0 || quality.backgroundCoverage!=0.0) return fail("fresh references do not distinguish target and background");
    report<<"PASS: fresh target=100%, background=0%, crosshair excluded\n";
    // A foreign strip leaves the center point exposed but contaminates the
    // analysis ROI. It must not verify a match or replace a saved reference.
    if(!activate()) return skip("partial-overlay focus unavailable");
    cleanup.blocker=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
        static_cast<int>(roi->x),static_cast<int>(roi->y),1,static_cast<int>(roi->height),nullptr,nullptr,wc.hInstance,nullptr);
    if(!cleanup.blocker) return fail("partial-overlay fixture unavailable");
    ShowWindow(cleanup.blocker,SW_SHOWNOACTIVATE);
    SetWindowPos(cleanup.blocker,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    UpdateWindow(cleanup.blocker);GdiFlush();DwmFlush();Sleep(40);pump();
    const POINT center{static_cast<LONG>(roi->x+roi->width/2),static_cast<LONG>(roi->y+roi->height/2)};
    if(!app.permittedAt(center) || app.permittedRegion(*roi)) {
        RECT r{};GetWindowRect(cleanup.blocker,&r);DWORD cloaked=0;
        DwmGetWindowAttribute(cleanup.blocker,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        std::cerr<<"partial_overlay center_permitted="<<app.permittedAt(center)
            <<" region_permitted="<<app.permittedRegion(*roi)<<" visible="<<IsWindowVisible(cleanup.blocker)
            <<" cloaked="<<cloaked<<" bounds="<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom
            <<" roi="<<roi->x<<','<<roi->y<<','<<roi->width<<','<<roi->height<<'\n';
        return fail("partial overlap bypassed ROI guard");
    }
    app.running=true;app.autoClick=true;app.settings.dryRun=true;app.update();
    if(app.sample.frame || app.verified || app.diagnosticCount || state.downs || state.ups) return fail("partial overlap entered detection/input");
    app.stop();DestroyWindow(cleanup.blocker);cleanup.blocker=nullptr;
    report<<"PASS: partial ROI overlay blocks detection with an exposed center\n";
    auto sameTarget=[&] {
        if(!app.targetReference || app.targetReference->width()!=retained.width() || app.targetReference->height()!=retained.height()) return false;
        for(int row=0;row<retained.height();++row) for(int col=0;col<retained.width();++col) {
            const auto a=retained.at(col,row),b=app.targetReference->at(col,row);
            if(a.red!=b.red || a.green!=b.green || a.blue!=b.blue || a.alpha!=b.alpha) return false;
        }
        return true;
    };
    if(!start(20) || app.snapshotPurpose!=Preview::SnapshotPurpose::Color || GetForegroundWindow()!=target) return fail("color snapshot did not automatically activate the selected window");
    const auto colorDeadline=app.aimPickDeadline;SendMessageW(app.window,WM_HOTKEY,9,0);app.selectionTick(colorDeadline+1);
    if(app.aimPickDeadline || !sameTarget() || app.color.target.red!=231) return fail("cancelled color snapshot changed state");
    EnableWindow(target,FALSE);
    const bool disabledStarted=start(31);
    EnableWindow(target,TRUE);
    if(disabledStarted || app.aimPickDeadline || app.autoClick || !sameTarget() || app.status!=L"Выбранное окно недоступно для активации") return fail("disabled window allowed snapshot activation");
    if(!start(31)) return fail("retry snapshot did not start");
    cleanup.blocker=CreateWindowExW(WS_EX_TOPMOST,L"STATIC",L"Owned focus-loss test",WS_POPUP|WS_VISIBLE,x,y,240,240,nullptr,nullptr,wc.hInstance,nullptr);
    if(!cleanup.blocker) return skip("focus-loss window unavailable");
    SetForegroundWindow(cleanup.blocker);SetFocus(cleanup.blocker);pump();
    if(GetForegroundWindow()!=cleanup.blocker) return skip("focus-loss window could not activate");
    app.selectionTick(app.aimPickDeadline);
    if(app.aimPickDeadline || !sameTarget() || !app.backgroundReference) return fail("focus loss replaced a reference");
    DestroyWindow(cleanup.blocker);cleanup.blocker=nullptr;
    state.color=RGB(0,0,0);state.crosshair=false;InvalidateRect(target,nullptr,FALSE);
    if(!activate()) return skip("black-frame test focus unavailable");
    probe=app.capture.capture(*roi);if(!probe.frame || !probe.allBlack) return skip("black reference pixels unavailable or covered");
    if(!start(31)) return fail("black-frame snapshot did not start");
    if(GetForegroundWindow()!=target) return fail("black-frame snapshot did not automatically activate the selected window");
    app.selectionTick(app.aimPickDeadline);
    if(app.aimPickDeadline || !sameTarget()) return fail("black frame replaced the target");
    state.color=RGB(231,67,19);state.crosshair=true;InvalidateRect(target,nullptr,FALSE);
    if(!activate()) return skip("F9 test focus unavailable");
    if(!start(32)) return fail("F9 snapshot did not start");
    const auto cancelledDeadline=app.aimPickDeadline;SendMessageW(app.window,WM_HOTKEY,9,0);app.selectionTick(cancelledDeadline+1);
    if(app.aimPickDeadline || !sameTarget() || !app.backgroundReference || app.backgroundReference->at(0,0).green!=255) return fail("cancelled snapshot overwrote a reference");
    if(app.running || app.autoClick || app.syntheticDown || app.verified || app.clickCount || app.diagnosticCount || state.downs || state.ups || app.sample.frame || !app.displayFrame || app.displayFrame->at(0,0).red!=9) return fail("reference capture changed input, verification or display state");
    DestroyWindow(target);cleanup.target=nullptr;app.refreshTargetButton();
    if(app.targetReference || app.backgroundReference || app.targetWindow) return fail("closed target retained references");
    report<<"PASS: focus loss, black frame, F9 cancellation, no input, unchanged display and closed-window cleanup\n";
    return 0;
}
int liveClickTest(Preview& app,bool centerMode=false,bool guided=false,bool stress=false) {
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::ofstream report(std::filesystem::path(executable).parent_path()/(stress?L"pulse-test-report.txt":guided?L"guided-test-report.txt":centerMode?L"center-test-report.txt":L"click-test-report.txt"));
    report<<std::unitbuf<<"stage=setup guided="<<guided<<"\n";
    POINT cursor{};
    if(!GetCursorPos(&cursor) || Preview::inputHeld()) {report<<"SKIP: cursor unavailable or physical input held\n";return 77;}
    HWND previous=GetForegroundWindow();ClickTestTarget state;state.crosshair=centerMode;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=clickTestProcedure;wc.lpszClassName=L"TriggerLabHoverInputTest";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    // Center mode deliberately samples away from the physical pointer. A
    // computer-use pointer highlight may cover the cursor but not this ROI.
    int sceneX=cursor.x-(centerMode?30:60),sceneY=cursor.y-(centerMode?30:60);
    if(centerMode) {
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor)) {
            if(cursor.x+210>monitor.rcMonitor.right) sceneX=cursor.x-210;
            if(cursor.y+210>monitor.rcMonitor.bottom) sceneY=cursor.y-210;
        }
    }
    HWND target=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab click test",WS_POPUP|WS_VISIBLE,sceneX,sceneY,centerMode?240:120,centerMode?240:120,nullptr,nullptr,wc.hInstance,&state);
    if(!target) {report<<"SKIP: cannot create test window\n";return 77;}
    app.targetWindow=target;GetWindowThreadProcessId(target,&app.targetProcess);
    UpdateWindow(target);
    // The shell/CI may own foreground permission. Temporarily attach only for
    // activating this test window, then detach before any capture or input.
    const DWORD foregroundThread=GetWindowThreadProcessId(previous,nullptr),thread=GetCurrentThreadId();
    const bool attached=foregroundThread && foregroundThread!=thread && AttachThreadInput(thread,foregroundThread,TRUE);
    SetForegroundWindow(target);SetActiveWindow(target);SetFocus(target);
    if(attached) AttachThreadInput(thread,foregroundThread,FALSE);
    auto pump=[] {pumpTestMessages();};
    pump();
    int result=0;
    if(!app.permittedAt(cursor)) {report<<"SKIP: test window cannot receive focus\n";result=77;}
    else {
        app.settings={};app.settings.center=centerMode;app.settings.dryRun=true;
        app.color.target={19,67,231,255};app.color.channelTolerance=0;app.color.space=triggerlab::ColorSpace::RGB;
        app.syncSettings();app.syncColor();if(!app.saveAll()) return 2;app.running=true;
        if(guided) {
            report<<"stage=save-guided-settings\n";
            app.syncSettings();app.syncColor();app.command(3);if(!app.saveAll()) {app.stop();DestroyWindow(target);return 2;}
            // Selecting a color focuses the analyzer; resume only our own test scene.
            SetForegroundWindow(target);SetActiveWindow(target);SetFocus(target);
        }
        app.gate.setTiming(70ms,50ms);
        GdiFlush();DwmFlush();Sleep(30);pump();app.update();
        const bool reference=centerMode?app.mask.found && app.mask.coverage==1:
            app.sample.cursorPixel && triggerlab::matchesColor(*app.sample.cursorPixel,app.color);
        if(!reference) app.running=false;
        const auto before=app.diagnosticCount;
        if(guided && reference) {report<<"stage=arm-diagnostic\n";app.guidedAction();}
        else {SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);app.command(8);}
        if(!reference) {report<<"SKIP: test reference pixels unavailable or covered by a desktop/pointer overlay\n";result=77;}
        else if(!app.autoClick) {report<<"SKIP: F9 unavailable\n";result=77;}
        else {
            auto observe=[&](std::chrono::milliseconds duration,bool finishOnVerification=false) {
                const auto until=triggerlab::Clock::now()+duration;
                while(triggerlab::Clock::now()<until) {
                    // Never deliver input to any window except this owned test target.
                    POINT point{};if(!GetCursorPos(&point) || !app.permittedAt(point)) return false;
                    app.update();pump();Sleep(10);
                    if(finishOnVerification && app.verified) return true;
                }
                return true;
            };
            report<<"stage=observe-diagnostic\n";
            const bool dryStable=observe(guided?1000ms:250ms,guided);
            auto feedbackMatches=[&](const wchar_t* expected,bool realArmed,const char* stage) {
                if(!guided) return true;
                app.syncActions();
                wchar_t header[512]{},hint[2048]{},summary[2048]{};
                GetWindowTextW(app.headerStatus,header,512);GetWindowTextW(app.homeHint,hint,2048);GetWindowTextW(app.homeSummary,summary,2048);
                const bool passed=app.currentFeedback().title==expected && std::wstring(header).find(expected)!=std::wstring::npos &&
                    std::wstring(header).find(realArmed?L"автоклики включены":L"без кликов")!=std::wstring::npos && hint[0] &&
                    std::wstring(summary).find(L"Клики: "+std::to_wstring(app.clickCount)+L" ")!=std::wstring::npos;
                report<<"feedback="<<stage<<" passed="<<passed<<" clicks="<<app.clickCount<<" diagnostics="<<app.diagnosticCount<<'\n';return passed;
            };
            const bool dryFeedback=feedbackMatches(L"Проверка пройдена",false,"diagnostic");
            wchar_t nextButton[128]{};GetWindowTextW(GetDlgItem(app.window,22),nextButton,128);
            const bool dryPassed=dryStable && dryFeedback && app.diagnosticCount==before+1 && state.downs==0 && state.ups==0 && !app.lastPress && (!guided || (app.verified && app.wizardStep()==3 && app.settings.dryRun && std::wstring(nextButton)==L"Включить клики"));
            report<<"dry_run="<<dryPassed<<" center_mode="<<centerMode<<"\n";
            report<<"stable="<<dryStable<<" before="<<before<<" verified="<<app.verified<<" step="<<app.wizardStep()<<" ready="<<app.colorReady<<" diagnostic_mode="<<app.settings.dryRun<<" downs="<<state.downs<<" ups="<<state.ups<<"\n";
            const auto p=app.sample.cursorPixel?*app.sample.cursorPixel:app.sample.frame?app.sample.frame->at(app.sample.frame->width()/2,app.sample.frame->height()/2):triggerlab::Pixel{};
            report<<"dry rgb="<<int(p.red)<<","<<int(p.green)<<","<<int(p.blue)<<" mask="<<app.mask.coverage<<" held="<<Preview::inputHeld()<<" diagnostics="<<app.diagnosticCount<<" foreground="<<(GetForegroundWindow()==target)<<"\n";
            const int bytes=WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),nullptr,0,nullptr,nullptr);
            std::string reason(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,app.clickStatus.data(),static_cast<int>(app.clickStatus.size()),reason.data(),bytes,nullptr,nullptr);report<<reason<<"\n";
            if(!dryPassed) {
                report<<(dryStable?"FAIL: diagnostic did not verify the match without input\n":"SKIP: user cursor or foreground changed during diagnostic\n");
                app.stop();app.targetWindow=nullptr;DestroyWindow(target);if(previous && IsWindow(previous)) SetForegroundWindow(previous);return dryStable?2:77;
            }
            const auto start=triggerlab::Clock::now();
            if(stress) {
                app.settings.dryRun=false;std::array<std::vector<long long>,2> measurements;
                bool passed=true,stable=true;
                for(int batch=0;batch<2 && passed && stable;++batch) for(int i=0;i<20 && passed && stable;++i) {
                    app.settings.clickMs=batch==0?5:30;app.syncSettings();app.savedSettings=app.settings;app.verified=true;app.gate.reset();
                    const auto deadline=triggerlab::Clock::now()+1500ms;
                    const int expected=state.downs+1;
                    while(!app.syntheticDown && app.clickCount<static_cast<std::uint64_t>(expected) && triggerlab::Clock::now()<deadline) {
                        POINT point{};if(!GetCursorPos(&point) || !app.permittedAt(point)) {stable=false;break;}
                        app.update();pump();Sleep(1);
                    }
                    if(!stable) break;
                    if(!app.syntheticDown && app.clickCount<static_cast<std::uint64_t>(expected)) {passed=false;break;}
                    // Reproduce the UI timer defect: no message dispatch or update.
                    if(batch==0 && i==0) {
                        Sleep(100);
                        // Stop/cancel arriving after a completed pulse must not
                        // lose its count or relabel it as an interrupted press.
                        app.finishClick(false);
                        if(app.lastPress) report<<"ui_stall_release_us="<<app.lastPress->elapsed.count()<<'\n';
                    }
                    while(app.syntheticDown && triggerlab::Clock::now()<deadline) {app.update();pump();Sleep(1);}
                    pump();
                    POINT point{};if(!GetCursorPos(&point) || !app.permittedAt(point)) {stable=false;break;}
                    passed=!app.syntheticDown && app.lastPress && app.lastPress->releaseAccepted && !app.lastPress->interrupted && state.downs==expected && state.ups==expected && app.clickCount==static_cast<std::uint64_t>(expected);
                    if(!passed) break;
                    const auto elapsed=app.lastPress->elapsed.count();measurements[batch].push_back(elapsed);
                    passed=elapsed>=app.settings.clickMs*1000 && (batch!=0 || i!=0 || elapsed<100000);
                }
                app.stop();pump();
                for(int batch=0;batch<2;++batch) if(!measurements[batch].empty()) {
                    auto& values=measurements[batch];std::sort(values.begin(),values.end());
                    report<<"requested_ms="<<(batch==0?5:30)<<" samples="<<values.size()<<" min_us="<<values.front()<<" median_us="<<values[values.size()/2]<<" max_us="<<values.back()<<'\n';
                }
                report<<"downs="<<state.downs<<" ups="<<state.ups<<" completed="<<app.clickCount<<'\n';
                report<<(!stable?"SKIP: cursor or foreground changed\n":passed?"PASS: pulse series, balanced input and release independent of UI pumping\n":"FAIL: pulse series or UI stall extended the press\n");
                app.targetWindow=nullptr;DestroyWindow(target);if(previous && IsWindow(previous)) SetForegroundWindow(previous);return !stable?77:passed?0:2;
            }
            if(guided) {report<<"stage=arm-real-input\n";app.guidedAction();}else {app.settings.dryRun=false;app.verified=true;app.syncSettings();app.savedSettings=app.settings;app.gate.reset();}
            if(guided && (!app.autoClick || app.settings.dryRun)) {
                const auto& message=app.notice;const int count=WideCharToMultiByte(CP_UTF8,0,message.data(),static_cast<int>(message.size()),nullptr,0,nullptr,nullptr);
                std::string text(count,'\0');WideCharToMultiByte(CP_UTF8,0,message.data(),static_cast<int>(message.size()),text.data(),count,nullptr,nullptr);
                report<<"FAIL: guided real-input activation: "<<text<<'\n';
                app.stop();app.targetWindow=nullptr;DestroyWindow(target);if(previous && IsWindow(previous)) SetForegroundWindow(previous);return 2;
            }
            bool stable=observe(300ms);
            const auto first=state.clicks.empty()?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks.front()-start).count();
            bool passed=dryPassed && stable && state.downs==1 && state.ups==1 && first>=70 && (!guided || !app.settings.dryRun);
            passed=feedbackMatches(L"Клик завершён",true,"first-click") && passed;
            report<<"first hover downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<first<<"\n";
            auto checkTiming=[&](int requested) {
                if(!app.lastPress) {report<<"FAIL: missing input timing\n";return false;}
                const auto& timing=*app.lastPress;
                report<<"send_input_requested_ms="<<timing.requestedMs<<" send_input_interval_us="<<timing.elapsed.count()<<" interrupted="<<timing.interrupted<<" release_accepted="<<timing.releaseAccepted<<'\n';
                return timing.requestedMs==requested && timing.elapsed>=std::chrono::milliseconds{requested} && !timing.interrupted && timing.releaseAccepted;
            };
            passed=checkTiming(30) && passed;
            state.color=RGB(50,50,50);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            stable=observe(100ms) && stable;
            passed=passed && state.downs==1;
            passed=feedbackMatches(L"Ожидание мишени",true,"background") && passed;
            app.settings.clickMs=5;app.syncSettings();app.savedSettings=app.settings;
            state.color=RGB(231,67,19);InvalidateRect(target,nullptr,FALSE);UpdateWindow(target);
            const auto secondStart=triggerlab::Clock::now();stable=observe(300ms) && stable;
            const auto second=state.clicks.size()<2?0:std::chrono::duration_cast<std::chrono::milliseconds>(state.clicks[1]-secondStart).count();
            passed=passed && state.downs==2 && state.ups==2 && second>=70;
            passed=checkTiming(5) && passed;
            if(state.releases.size()==2 && state.clicks.size()==2) {
                const auto held=std::chrono::duration_cast<std::chrono::milliseconds>(state.releases[0]-state.clicks[0]).count();
                // Both messages can already be queued when the UI pumps them.
                // Handler timestamps measure dispatch, not the button hold.
                // checkTiming above validates the actual SendInput interval.
                report<<"received_dispatch_interval_ms="<<held<<"\n";
                const auto secondHeld=std::chrono::duration_cast<std::chrono::microseconds>(state.releases[1]-state.clicks[1]).count();
                report<<"second_received_dispatch_interval_us="<<secondHeld<<"\n";
            }
            report<<"reacquire downs="<<state.downs<<" ups="<<state.ups<<" delay_ms="<<second<<"\n";
            app.settings.clickMs=150;app.syncSettings();app.savedSettings=app.settings;app.gate.reset();const auto holdDeadline=triggerlab::Clock::now()+500ms;
            while(!app.syntheticDown && triggerlab::Clock::now()<holdDeadline) {app.update();pump();Sleep(5);}
            const bool interrupted=app.syntheticDown;if(guided) SendMessageW(app.window,WM_HOTKEY,9,0);else app.disableClicks();pump();
            report<<"stop_released_pending_press="<<interrupted<<" downs="<<state.downs<<" ups="<<state.ups<<"\n";
            stable=observe(100ms) && stable;passed=passed && interrupted && !app.syntheticDown && state.downs==3 && state.ups==3 && (!guided || (!app.running && !app.autoClick));
            passed=feedbackMatches(L"Готово к запуску",false,"stopped") && passed;
            passed=passed && app.lastPress && app.lastPress->requestedMs==150 && app.lastPress->interrupted && app.lastPress->releaseAccepted;
            if(app.lastPress) {
                const auto retained=app.lastPress->elapsed;
                app.finishClick();app.disableClicks();
                passed=passed && app.lastPress->elapsed==retained && app.lastPress->interrupted;
                report<<"stop_send_input_interval_us="<<retained.count()<<" timing_retained="<<(app.lastPress->elapsed==retained)<<'\n';
            }
            if(passed && centerMode) {
                ClickTestTarget sentinelState;
                HWND sentinel=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TriggerLab input guard test",WS_POPUP,sceneX,sceneY,240,240,nullptr,nullptr,wc.hInstance,&sentinelState);
                if(!sentinel) passed=false;
                for(int stage=0;stage<2 && passed && stable;++stage) {
                    ShowWindow(sentinel,SW_HIDE);
                    SetWindowPos(target,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                    SetForegroundWindow(target);pump();
                    POINT point{};
                    if(!GetCursorPos(&point) || !app.permittedAt(point)) {stable=false;break;}
                    app.running=true;app.settings.dryRun=false;app.settings.clickMs=150;app.syncSettings();app.savedSettings=app.settings;app.verified=true;
                    SendMessageW(app.autoControl,BM_SETCHECK,BST_CHECKED,0);app.command(8);
                    if(!app.autoClick) {stable=false;break;}
                    const auto deadline=triggerlab::Clock::now()+1000ms;
                    while(!app.syntheticDown && triggerlab::Clock::now()<deadline) {
                        if(!GetCursorPos(&point) || !app.permittedAt(point)) {stable=false;break;}
                        app.update();pump();Sleep(1);
                    }
                    if(!stable) break;
                    if(!app.syntheticDown) {passed=false;break;}
                    ShowWindow(sentinel,SW_SHOWNOACTIVATE);
                    SetWindowPos(sentinel,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                    if(stage==1) {DestroyWindow(target);target=nullptr;}
                    SetForegroundWindow(sentinel);
                    if(GetForegroundWindow()!=sentinel) {stable=false;break;}
                    app.update();pump();
                    const bool released=app.lastPress && app.lastPress->interrupted && app.lastPress->releaseAccepted;
                    // If a slow desktop let the complete 150 ms elapse before
                    // the focus operation, this did not exercise cancellation.
                    if(app.lastPress && !app.lastPress->interrupted && app.lastPress->elapsed>=150ms) {stable=false;break;}
                    const int beforeDowns=state.downs+sentinelState.downs;
                    Sleep(180);app.update();pump();
                    passed=released && !app.syntheticDown && !app.autoClick && sentinelState.downs==0 && state.downs+sentinelState.downs==beforeDowns && state.ups+sentinelState.ups==beforeDowns;
                    report<<(stage==0?"focus_loss":"target_closed")<<" released="<<released<<" trigger_off="<<!app.autoClick<<" downs="<<beforeDowns<<" ups="<<state.ups+sentinelState.ups<<" other_downs="<<sentinelState.downs<<'\n';
                }
                app.stop();DestroyWindow(sentinel);
            }
            report<<(passed?"PASS: actual SendInput received once per hover, dwell >=70ms, stop blocks input\n":stable?"FAIL: real hover input\n":"SKIP: user cursor or foreground changed during test\n");
            if(!passed) {
                report<<"capture_status="<<static_cast<int>(app.sample.status)<<" matching="<<(app.sample.cursorPixel && triggerlab::matchesColor(*app.sample.cursorPixel,app.color))<<"\n";
                result=stable?2:77;
            }
        }
    }
    app.stop();app.targetWindow=nullptr;DestroyWindow(target);if(previous && IsWindow(previous)) SetForegroundWindow(previous);
    return result;
}
}
