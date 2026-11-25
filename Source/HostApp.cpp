// Atmos Bridge Host: Atmos Bridge as a standalone Windows app (no plugin host needed).
//
//   player -> CABLE In (VB-Audio) -> [direct loopback] -> routing -> plugin chain (stereo plugins / upmixer)
//          -> Atmos 7.1.4: Windows spatial audio (Dolby Atmos for home theater, MAT over HDMI)
//          -> 2ch Stereo:  WASAPI exclusive PCM 2.0 (the AV receiver's upmixers)
//
// The app owns one AtmosBridgeProcessor in host mode (it runs on its own threads: the loopback capture
// and the output engine), shows its editor in a window, and lives in the system tray:
//   - tray icon with a status dot, tooltip, left click = window, right click = menu
//   - minimise / close (X) to the tray, start with Windows in the tray (HKCU Run key)
//   - one instance only (starting it again opens the window)
//   - everything (chain with each plugin's settings, routing, mapping, format, mode, ...) is saved
//     to %APPDATA%\Atmos Bridge Host\host-state.dat as it changes.

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "engine/Platform.h"

using namespace ab;

namespace
{
juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::File appFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Atmos Bridge Host");
    dir.createDirectory();
    return dir;
}

juce::File stateFile() { return appFolder().getChildFile ("host-state.dat"); }

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "AtmosBridgeHost";
    o.folderName = "Atmos Bridge Host";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    return o;
}

const juce::String kRunValue ("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Atmos Bridge Host");

// Tray icon: speaker on a dark tile, status dot bottom right.
juce::Image makeTrayIcon (juce::Colour status, int size)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const float s = (float) size;
    g.setColour (juce::Colour (0xff111318));
    g.fillRoundedRectangle (0.0f, 0.0f, s, s, s * 0.18f);
    const auto green = juce::Colour (0xff39ff14);
    juce::Path body;
    body.startNewSubPath (s * 0.14f, s * 0.38f);
    body.lineTo (s * 0.28f, s * 0.38f);
    body.lineTo (s * 0.46f, s * 0.20f);
    body.lineTo (s * 0.46f, s * 0.80f);
    body.lineTo (s * 0.28f, s * 0.62f);
    body.lineTo (s * 0.14f, s * 0.62f);
    body.closeSubPath();
    g.setColour (green);
    g.fillPath (body);
    for (float r : { 0.16f, 0.29f })
    {
        juce::Path wave;
        wave.addCentredArc (s * 0.46f, s * 0.5f, s * r, s * r, 0.0f, 0.75f, 2.39f, true);
        g.strokePath (wave, juce::PathStrokeType (s * 0.075f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float d = s * 0.40f;
    const juce::Rectangle<float> dot (s - d, s - d, d, d);
    g.setColour (juce::Colour (0xff111318));
    g.fillEllipse (dot.expanded (s * 0.05f));
    g.setColour (status);
    g.fillEllipse (dot);
    return img;
}
} // namespace

class HostApp;

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (AtmosBridgeProcessor& p, std::function<void()> onClose, std::function<void()> onMinimise)
        : juce::DocumentWindow ("Atmos Bridge Host", juce::Colour (0xff07060d), juce::DocumentWindow::allButtons),
          closeFn (std::move (onClose)), minimiseFn (std::move (onMinimise))
    {
        setUsingNativeTitleBar (true);
        auto* ed = p.createEditor();
        ed->setResizable (false, false);       // the window does the resizing (the editor scales to its width)
        setContentOwned (ed, true);
        setResizable (true, false);
        setResizeLimits (kBaseWidth / 3, kHostBaseHeight / 3, 8000, 8000);

        // First size: within the usable screen area (Windows scaling 125-150 %, taskbar), title bar included.
        int w = getWidth(), h = getHeight();
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = d->userArea.reduced (16);
            const int titleBar = 40;
            const float k = juce::jmin (1.0f, (float) area.getWidth() / (float) w, (float) (area.getHeight() - titleBar) / (float) h);
            if (k < 1.0f)
            {
                w = juce::roundToInt ((float) w * k);
                h = juce::roundToInt ((float) h * k);
            }
        }
        centreWithSize (w, h);
    }

    void closeButtonPressed() override { if (closeFn) closeFn(); }
    void minimisationStateChanged (bool isNowMinimised) override
    {
        juce::DocumentWindow::minimisationStateChanged (isNowMinimised);
        if (isNowMinimised && minimiseFn) minimiseFn();
    }

private:
    std::function<void()> closeFn, minimiseFn;
};

//==============================================================================
class TrayIcon final : public juce::SystemTrayIconComponent, private juce::Timer
{
public:
    explicit TrayIcon (HostApp& a) : app (a)
    {
        update();
        startTimer (1000);
    }

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override { update(); }
    void update();
    void showMenu();

    HostApp& app;
    juce::Colour shown;
    juce::String shownTip;
};

//==============================================================================
class HostApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "Atmos Bridge Host"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        FileLogger::get().setBaseName ("atmosbridge-host");
        logLine ("Atmos Bridge Host " + getApplicationVersion().toStdString() + " starting (" + commandLine.toStdString() + ")");

        settings = std::make_unique<juce::PropertiesFile> (settingsOptions());
        proc = std::make_unique<AtmosBridgeProcessor>();
        proc->setHostApp (true);
        proc->prepareToPlay (48000.0, 480);
        {
            juce::MemoryBlock mb;
            if (stateFile().loadFileAsData (mb) && mb.getSize() > 0)
            {
                proc->setStateInformation (mb.getData(), (int) mb.getSize());
                lastSaved = mb;
            }
            else
            {
                proc->setMode (LatencyMode::Low);   // first start: low latency (direct loopback, self-tuning margin)
            }
        }
        proc->onStateChanged = [this] { dirtyTicks = 2; };
        proc->onHideToTray = [this] { hideToTray(); };

        window = std::make_unique<MainWindow> (*proc, [this] { closePressed(); }, [this] { minimised(); });
        tray = std::make_unique<TrayIcon> (*this);
        syncAutostart();

        if (commandLine.contains ("--tray"))
            window->setVisible (false);
        else
            showWindow();
        startTimer (1000);

       #if AB_TEST_HOOKS
        // Linux smoke test: change something, then quit after a while (shutdown + state file)
        if (const char* q = std::getenv ("AB_HOST_QUIT_MS"))
        {
            proc->setOutputFormat (OutputFormat::Stereo);
            proc->setMasterDb (-7.5f);
            juce::Timer::callAfterDelay (juce::String (q).getIntValue(), [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
        }
       #endif
    }

    void shutdown() override
    {
        stopTimer();
        saveState();
        tray.reset();
        window.reset();
        proc.reset();
        settings.reset();
        logLine ("Atmos Bridge Host stopped");
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    //==========================================================================
    AtmosBridgeProcessor& getProcessor() noexcept { return *proc; }

    void showWindow()
    {
        if (window == nullptr) return;
        window->setVisible (true);
        window->setMinimised (false);
        window->toFront (true);
    }

    void hideToTray()
    {
        if (window == nullptr) return;
        window->setVisible (false);
        if (tray != nullptr && ! settings->getBoolValue ("trayHintShown", false))
        {
            settings->setValue ("trayHintShown", true);
            settings->saveIfNeeded();
            tray->showInfoBubble ("Atmos Bridge Host", tr ("트레이에서 계속 실행 중입니다. 아이콘을 누르면 창이 열립니다."));
        }
    }

    bool getOption (const char* key) const
    {
        const bool def = juce::String (key) != "autostart";   // minimise / close to tray: on, autostart: off
        return settings->getBoolValue (key, def);
    }

    void setOption (const char* key, bool on)
    {
        settings->setValue (key, on);
        settings->saveIfNeeded();
        if (juce::String (key) == "autostart") syncAutostart();
        logLine (std::string ("Option ") + key + (on ? " on" : " off"));
    }

private:
    void closePressed()
    {
        if (getOption ("closeToTray")) hideToTray();
        else quit();
    }

    void minimised()
    {
        if (! getOption ("minimiseToTray")) return;
        juce::MessageManager::callAsync ([this] { if (window != nullptr && window->isMinimised()) hideToTray(); });
    }

    void syncAutostart()
    {
       #if JUCE_WINDOWS
        const bool want = getOption ("autostart");
        if (want)
        {
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
            juce::WindowsRegistry::setValue (kRunValue, "\"" + exe + "\" --tray");
        }
        else if (juce::WindowsRegistry::valueExists (kRunValue))
        {
            juce::WindowsRegistry::deleteValue (kRunValue);
        }
       #endif
    }

    void saveState()
    {
        if (proc == nullptr) return;
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        if (mb == lastSaved) return;
        if (stateFile().replaceWithData (mb.getData(), mb.getSize()))
            lastSaved = mb;
        else
            logLine ("Could not save " + stateFile().getFullPathName().toStdString());
    }

    void timerCallback() override
    {
        // Saved shortly after a change; every 30 s anyway (settings changed inside a plugin's own window).
        if (dirtyTicks > 0 && --dirtyTicks == 0) saveState();
        if (++ticks % 30 == 0) saveState();
    }

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<AtmosBridgeProcessor> proc;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<TrayIcon> tray;
    juce::MemoryBlock lastSaved;
    int dirtyTicks = 0, ticks = 0;
};

//==============================================================================
void TrayIcon::update()
{
    auto& p = app.getProcessor();
    juce::Colour col (0xff8f8bb8);
    juce::String state = tr ("정지");
    if (auto* s = p.getSharedOutput())
    {
        switch (s->engine.getStatus())
        {
            case EngineStatus::Running:      col = juce::Colour (0xff39ff14); state = tr ("출력 중"); break;
            case EngineStatus::Connecting:   col = juce::Colour (0xfffcee0a); state = tr ("연결 중"); break;
            case EngineStatus::SpatialOff:   col = juce::Colour (0xffff6b6b); state = tr ("공간 음향 꺼짐"); break;
            case EngineStatus::Busy:         col = juce::Colour (0xffff6b6b); state = tr ("AVR을 다른 앱이 사용 중"); break;
            case EngineStatus::ExclusiveOff: col = juce::Colour (0xffff6b6b); state = tr ("배타 모드 꺼짐"); break;
            case EngineStatus::NoDevice:     col = juce::Colour (0xffff6b6b); state = tr ("출력 장치 없음"); break;
            case EngineStatus::Error:        col = juce::Colour (0xffff6b6b); state = tr ("오류"); break;
            default: break;
        }
        const auto ds = p.getDirectStatus();
        using DS = AtmosBridgeProcessor::DirectStatus;
        if (s->engine.getStatus() == EngineStatus::Running && ds.state != DS::Running && ds.state != DS::Settling && ds.reason.isNotEmpty())
        {
            col = juce::Colour (0xfffcee0a);
            state = tr ("입력: ") + ds.reason;
        }
    }
    if (p.isBypassed())     { col = juce::Colour (0xfffcee0a); state = "BYPASS"; }
    if (p.isMasterMuted())  { col = juce::Colour (0xff8f8bb8); state = "MUTE"; }

    const auto ds = p.getDirectStatus();
    const double pkt = ds.packetMs > 0.0 ? ds.packetMs : 10.0;
    const double total = pkt + juce::jmax (0.0, ds.marginMs) + p.getPluginLatencyMs() + p.getOutputPeriodMs();
    const juce::String tip = tr ("Atmos Bridge \xc2\xb7 ") + state + "\n"
                           + (p.getOutputFormat() == OutputFormat::Stereo ? tr ("2ch Stereo") : tr ("Atmos 7.1.4"))
                           + tr (" \xc2\xb7 지연 \xe2\x89\x88 ") + juce::String (total, 1) + " ms";

    if (col != shown)
    {
        shown = col;
        setIconImage (makeTrayIcon (col, 32), makeTrayIcon (col, 32));
    }
    if (tip != shownTip)
    {
        shownTip = tip;
        setIconTooltip (tip);
    }
}

void TrayIcon::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showMenu();
    else app.showWindow();
}

void TrayIcon::mouseDoubleClick (const juce::MouseEvent&) { app.showWindow(); }

void TrayIcon::showMenu()
{
    auto& p = app.getProcessor();
    const bool stereo = p.getOutputFormat() == OutputFormat::Stereo;
    const int mode = (int) p.getMode();

    juce::PopupMenu modes;
    modes.addItem (20, tr ("기본"), true, mode == 0);
    modes.addItem (21, tr ("저지연"), true, mode >= 1);

    juce::PopupMenu m;
    m.addSectionHeader ("Atmos Bridge Host " + juce::JUCEApplication::getInstance()->getApplicationVersion());
    m.addItem (1, tr ("창 열기"));
    m.addSeparator();
    m.addSectionHeader (tr ("출력 형식"));
    m.addItem (10, tr ("Atmos 7.1.4"), true, ! stereo);
    m.addItem (11, tr ("2ch Stereo"), true, stereo);
    m.addSubMenu (tr ("모드"), modes);
    m.addSeparator();
    m.addItem (30, "BYPASS", true, p.isBypassed());
    m.addItem (31, "MUTE", true, p.isMasterMuted());
    m.addSeparator();
    m.addItem (40, tr ("최소화하면 트레이로"), true, app.getOption ("minimiseToTray"));
    m.addItem (41, tr ("닫기(X)도 트레이로 (종료는 이 메뉴)"), true, app.getOption ("closeToTray"));
    m.addItem (42, tr ("Windows 시작 시 트레이로 자동 실행"), true, app.getOption ("autostart"));
    m.addSeparator();
    m.addItem (99, tr ("종료"));

    juce::Process::makeForegroundProcess();   // so the menu closes when clicking elsewhere
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        auto& pr = app.getProcessor();
        switch (r)
        {
            case 1:  app.showWindow(); break;
            case 10: pr.setOutputFormat (OutputFormat::Atmos); break;
            case 11: pr.setOutputFormat (OutputFormat::Stereo); break;
            case 20: case 21: pr.setMode ((LatencyMode) (r - 20)); break;
            case 30: pr.setBypassed (! pr.isBypassed()); break;
            case 31: pr.setMasterMuted (! pr.isMasterMuted()); break;
            case 40: app.setOption ("minimiseToTray", ! app.getOption ("minimiseToTray")); break;
            case 41: app.setOption ("closeToTray", ! app.getOption ("closeToTray")); break;
            case 42: app.setOption ("autostart", ! app.getOption ("autostart")); break;
            case 99: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
        update();
    });
}

START_JUCE_APPLICATION (HostApp)
