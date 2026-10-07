#include <JuceHeader.h>

class BuilderJob final : public juce::Thread
{
public:
    using LogFn = std::function<void(const juce::String&)>;
    using DoneFn = std::function<void(bool, juce::File, const juce::String&)>;

    BuilderJob(juce::File sourceRoot, juce::File cacheRoot, LogFn log, DoneFn done)
        : Thread("Kyoto VST3 Builder"),
          source(std::move(sourceRoot)),
          cache(std::move(cacheRoot)),
          appendLog(std::move(log)),
          finished(std::move(done))
    {
    }

    void run() override
    {
        const auto buildDir = cache.getChildFile("build");
        const auto distDir = cache.getChildFile("dist");
        buildDir.createDirectory();
        distDir.createDirectory();

        appendLog("=== Kyoto VST3 Quick Builder ===");
        appendLog("Source: " + source.getFullPathName());
        appendLog("Build:  " + buildDir.getFullPathName());
        appendLog("");

        if (threadShouldExit())
            return;

        const auto cmake = findCMake();
        if (cmake.isEmpty())
        {
            finish(false, {}, "CMake was not found on this PC.");
            return;
        }

        appendLog("CMake: " + cmake);

        // Prefer Ninja when it is already installed. Otherwise use the
        // Visual Studio 2022 generator, which requires no environment setup.
        const auto ninja = findOnPath("ninja.exe");
        juce::String configure;

        if (ninja.isNotEmpty())
        {
            appendLog("Generator: Ninja (fast incremental mode)");
            configure = quote(cmake) + " -S " + quote(source.getFullPathName())
                      + " -B " + quote(buildDir.getFullPathName())
                      + " -G Ninja -DCMAKE_BUILD_TYPE=Release";
        }
        else
        {
            appendLog("Generator: Visual Studio 17 2022 (Ninja not installed)");
            configure = quote(cmake) + " -S " + quote(source.getFullPathName())
                      + " -B " + quote(buildDir.getFullPathName())
                      + " -G \"Visual Studio 17 2022\" -A x64";
        }

        appendLog("");
        appendLog("> " + configure);

        int exitCode = -1;
        if (!runCapture(configure, exitCode) || exitCode != 0)
        {
            finish(false, {}, "CMake configuration failed (exit " + juce::String(exitCode) + ").");
            return;
        }

        if (threadShouldExit())
            return;

        juce::String buildCommand;
        if (ninja.isNotEmpty())
            buildCommand = quote(cmake) + " --build " + quote(buildDir.getFullPathName())
                         + " --parallel";
        else
            buildCommand = quote(cmake) + " --build " + quote(buildDir.getFullPathName())
                         + " --config Release --parallel";

        appendLog("");
        appendLog("> " + buildCommand);

        if (!runCapture(buildCommand, exitCode) || exitCode != 0)
        {
            finish(false, {}, "VST3 build failed (exit " + juce::String(exitCode) + ").");
            return;
        }

        if (threadShouldExit())
            return;

        juce::Array<juce::File> plugins;
        findVST3(buildDir, plugins);

        if (plugins.isEmpty())
        {
            finish(false, {}, "Build completed, but no .vst3 bundle was found.");
            return;
        }

        // Select the newest VST3 output and copy it into the persistent cache.
        std::sort(plugins.begin(), plugins.end(),
                  [](const juce::File& a, const juce::File& b)
                  {
                      return a.getLastModificationTime() > b.getLastModificationTime();
                  });

        const auto builtPlugin = plugins.getFirst();
        auto cachedPlugin = distDir.getChildFile(builtPlugin.getFileName());

        if (cachedPlugin.exists())
            cachedPlugin.deleteRecursively();

        if (!builtPlugin.copyDirectoryTo(cachedPlugin))
        {
            finish(false, {}, "Build succeeded, but the VST3 could not be copied into the cache.");
            return;
        }

        appendLog("");
        appendLog("BUILD SUCCESS");
        appendLog("Cached: " + cachedPlugin.getFullPathName());

        finish(true, cachedPlugin, "Build succeeded • launching plugin…");
    }

private:
    juce::File source, cache;
    LogFn appendLog;
    DoneFn finished;

    void finish(bool ok, const juce::File& file, const juce::String& message)
    {
        if (finished)
            juce::MessageManager::callAsync(
                [done = finished, ok, file, message] { done(ok, file, message); });
    }

    static juce::String quote(const juce::String& value)
    {
        return "\"" + value.replace("\"", "\\\"") + "\"";
    }

    static juce::String findOnPath(const juce::String& exe)
    {
        auto env = juce::SystemStats::getEnvironmentVariable("PATH", {});
        for (auto part : juce::StringArray::fromTokens(env, ";", ""))
        {
            auto candidate = juce::File(part.trim()).getChildFile(exe);
            if (candidate.existsAsFile())
                return candidate.getFullPathName();
        }
        return {};
    }

    static juce::String findCMake()
    {
        auto path = findOnPath("cmake.exe");
        if (path.isNotEmpty())
            return path;

        const juce::StringArray common =
        {
            R"(C:\Program Files\CMake\bin\cmake.exe)",
            R"(C:\Program Files (x86)\CMake\bin\cmake.exe)"
        };

        for (const auto& p : common)
            if (juce::File(p).existsAsFile())
                return p;

        return {};
    }

    bool runCapture(const juce::String& command, int& exitCode)
    {
        juce::ChildProcess process;

        if (!process.start(command, juce::ChildProcess::wantStdOut
                                      | juce::ChildProcess::wantStdErr))
        {
            appendLog("Could not start process.");
            exitCode = -1;
            return false;
        }

        char buffer[4096];

        while (!threadShouldExit())
        {
            const int n = process.readProcessOutput(buffer, sizeof(buffer) - 1);
            if (n > 0)
            {
                buffer[n] = 0;
                appendLog(juce::String::fromUTF8(buffer));
            }

            if (!process.isRunning())
                break;

            wait(10);
        }

        exitCode = process.getExitCode();
        return !threadShouldExit();
    }

    static void findVST3(const juce::File& root, juce::Array<juce::File>& out)
    {
        juce::DirectoryIterator it(root, true, "*.vst3",
                                   juce::File::findFilesAndDirectories);

        while (it.next())
        {
            if (it.getFile().isDirectory())
                out.add(it.getFile());
        }
    }
};

class HostView final : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    HostView()
    {
        addAndMakeVisible(buildMode);
        buildMode.setButtonText("BUILD MODE");
        buildMode.setToggleState(true, juce::dontSendNotification);
        buildMode.onClick = [this] { updateMode(); };

        addAndMakeVisible(modeLabel);
        modeLabel.setFont(juce::FontOptions(18.0f).withStyle("bold"));

        addAndMakeVisible(status);
        status.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(logBox);
        logBox.setMultiLine(true);
        logBox.setReadOnly(true);
        logBox.setScrollbarsShown(true);
        logBox.setFont(juce::FontOptions(13.0f));

        addAndMakeVisible(closeButton);
        closeButton.setButtonText("Unload VST3");
        closeButton.onClick = [this] { closePlugin(); };

        formatManager.addDefaultFormats();

        audioDeviceManager.initialiseWithDefaultDevices(0, 2);
        audioDeviceManager.addAudioCallback(&player);

        updateMode();
        setWantsKeyboardFocus(true);
    }

    ~HostView() override
    {
        if (job != nullptr)
            job->stopThread(3000);

        closePlugin();
        audioDeviceManager.removeAudioCallback(&player);
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override
    {
        return true;
    }

    void fileDragEnter(const juce::StringArray&, int, int) override
    {
        dropActive = true;
        repaint();
    }

    void fileDragExit(const juce::StringArray&) override
    {
        dropActive = false;
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        dropActive = false;

        if (files.isEmpty())
            return;

        juce::File dropped(files[0]);

        if (!buildMode.getToggleState())
        {
            auto vst = findVST3FromDrop(dropped);
            if (vst.exists())
                loadDroppedPlugin(vst);
            else
                status.setText("Drop a .vst3 folder.", juce::dontSendNotification);

            repaint();
            return;
        }

        // Accept either an extracted GitHub folder or a folder containing one.
        auto source = findProjectRoot(dropped);

        if (source.exists())
            startBuild(source);
        else
            status.setText("No CMakeLists.txt found in the dropped folder.",
                           juce::dontSendNotification);

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0d10));

        auto area = getLocalBounds().reduced(16);
        g.setColour(juce::Colour(0xff171b22));
        g.fillRoundedRectangle(area.toFloat(), 14.0f);

        auto drop = area.reduced(16);
        g.setColour(dropActive ? juce::Colour(0xff303947) : juce::Colour(0xff11151b));
        g.fillRoundedRectangle(drop.toFloat(), 12.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(26.0f).withStyle("bold"));
        g.drawText("Kyoto VST3 Quick Builder",
                   drop.withTrimmedTop(85).withHeight(45),
                   juce::Justification::centred);

        g.setFont(juce::FontOptions(15.0f));
        g.setColour(juce::Colours::lightgrey);
        g.drawText(buildMode.getToggleState()
                       ? "Drop a GitHub/CMake VST3 project • compile • cache • launch"
                       : "Drop a Windows .vst3 folder • load it as a native plugin",
                   drop.reduced(20).withTrimmedTop(135),
                   juce::Justification::centredTop);

        if (dropActive)
        {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillRoundedRectangle(drop.reduced(6).toFloat(), 14.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        auto top = r.removeFromTop(58);

        buildMode.setBounds(top.removeFromLeft(145));
        modeLabel.setBounds(top.removeFromLeft(270));
        closeButton.setBounds(top.removeFromRight(140));

        auto bottom = r.removeFromBottom(180);
        logBox.setBounds(bottom);
        status.setBounds(r.removeFromBottom(34));

        if (editor != nullptr)
            editor->setBounds(r.reduced(8));
    }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioProcessorPlayer player;
    juce::KnownPluginList knownPlugins;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<BuilderJob> job;

    juce::ToggleButton buildMode;
    juce::Label modeLabel, status;
    juce::TextEditor logBox;
    juce::TextButton closeButton;
    bool dropActive = false;

    static juce::File appData()
    {
        auto dir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("KyotoVST3QuickBuilder");
        dir.createDirectory();
        return dir;
    }

    void updateMode()
    {
        const bool build = buildMode.getToggleState();

        modeLabel.setText(build ? "BUILD → CACHE → LAUNCH"
                                : "NATIVE VST3 PLAYER",
                           juce::dontSendNotification);

        status.setText(build ? "Drop an extracted GitHub/CMake project folder"
                             : "Drop a .vst3 folder",
                       juce::dontSendNotification);
        repaint();
    }

    void appendLog(const juce::String& text)
    {
        juce::MessageManager::callAsync([this, text]
        {
            logBox.moveCaretToEnd();
            logBox.insertTextAtCaret(text.endsWithChar('\n') ? text : text + "\n");
        });
    }

    static juce::File findProjectRoot(const juce::File& dropped)
    {
        if (!dropped.isDirectory())
            return {};

        if (dropped.getChildFile("CMakeLists.txt").existsAsFile())
            return dropped;

        juce::Array<juce::File> children;
        juce::DirectoryIterator it(dropped, false, "*", juce::File::findDirectories);
        while (it.next())
            children.add(it.getFile());

        for (auto& child : children)
            if (child.getChildFile("CMakeLists.txt").existsAsFile())
                return child;

        return {};
    }

    static juce::File findVST3FromDrop(const juce::File& dropped)
    {
        if (dropped.isDirectory() && dropped.hasFileExtension(".vst3"))
            return dropped;

        if (!dropped.isDirectory())
            return {};

        juce::Array<juce::File> found;
        juce::DirectoryIterator it(dropped, true, "*.vst3",
                                   juce::File::findFilesAndDirectories);

        while (it.next())
            if (it.getFile().isDirectory())
                found.add(it.getFile());

        return found.isEmpty() ? juce::File() : found.getFirst();
    }

    static juce::String makeSignature(const juce::File& root)
    {
        juce::int64 size = 0;
        juce::int64 newest = 0;
        int count = 0;

        juce::DirectoryIterator it(root, true, "*",
                                   juce::File::findFiles);
        while (it.next())
        {
            auto file = it.getFile();
            const auto path = file.getFullPathName();

            if (path.containsIgnoreCase("\\build\\")
                || path.containsIgnoreCase("/build/")
                || path.containsIgnoreCase("\\.git\\")
                || path.containsIgnoreCase("/.git/"))
                continue;

            size += file.getSize();
            newest = juce::jmax(newest,
                                file.getLastModificationTime().toMilliseconds());
            ++count;
        }

        return juce::String(count) + ":" + juce::String(size) + ":"
             + juce::String(newest);
    }

    static void findVST3(const juce::File& root, juce::Array<juce::File>& out)
    {
        juce::DirectoryIterator it(root, true, "*.vst3",
                                   juce::File::findFilesAndDirectories);

        while (it.next())
            if (it.getFile().isDirectory())
                out.add(it.getFile());
    }

    void startBuild(const juce::File& source)
    {
        if (job != nullptr)
        {
            status.setText("A build is already running.", juce::dontSendNotification);
            return;
        }

        closePlugin();
        logBox.clear();
        status.setText("Preparing incremental build…", juce::dontSendNotification);

        auto name = source.getFileName().replaceCharacters(" ", "_")
                    .replaceCharacters("\\/:*?\"<>|", "_________");

        auto cacheRoot = appData().getChildFile("Builds").getChildFile(name);
        cacheRoot.createDirectory();

        const auto signature = makeSignature(source);
        const auto stamp = cacheRoot.getChildFile("source.signature.txt");
        const auto dist = cacheRoot.getChildFile("dist");

        if (stamp.existsAsFile() && stamp.loadFileAsString() == signature)
        {
            juce::Array<juce::File> cached;
            findVST3(dist, cached);

            if (!cached.isEmpty())
            {
                appendLog("CACHE HIT — source unchanged.");
                status.setText("Cached build found • launching…",
                               juce::dontSendNotification);
                loadDroppedPlugin(cached.getFirst());
                return;
            }
        }

        job = std::make_unique<BuilderJob>(
            source, cacheRoot,
            [this](const juce::String& s) { appendLog(s); },
            [this, stamp, signature](bool ok, juce::File file, const juce::String& msg)
            {
                job.reset();

                if (ok)
                {
                    stamp.replaceWithText(signature);
                    status.setText(msg, juce::dontSendNotification);
                    loadDroppedPlugin(file);
                }
                else
                {
                    status.setText(msg, juce::dontSendNotification);
                }
            });

        job->startThread();
    }

    void loadDroppedPlugin(const juce::File& file)
    {
        if (!file.isDirectory() || !file.hasFileExtension(".vst3"))
        {
            status.setText("Not a valid .vst3 folder.", juce::dontSendNotification);
            return;
        }

        closePlugin();

        juce::AudioPluginFormat* vst3 = nullptr;
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat(i);
            if (format->getName().containsIgnoreCase("VST3"))
            {
                vst3 = format;
                break;
            }
        }

        if (vst3 == nullptr)
        {
            status.setText("VST3 support is unavailable in this build.",
                           juce::dontSendNotification);
            return;
        }

        juce::OwnedArray<juce::PluginDescription> descriptions;

        if (!knownPlugins.scanAndAddFile(file.getFullPathName(), true, descriptions, *vst3)
            || descriptions.isEmpty())
        {
            status.setText("Could not identify the VST3.",
                           juce::dontSendNotification);
            return;
        }

        juce::String error;
        plugin = formatManager.createPluginInstance(
            *descriptions[0], 48000.0, 512, error);

        if (plugin == nullptr)
        {
            status.setText("VST3 failed to load.", juce::dontSendNotification);
            appendLog(error);
            return;
        }

        plugin->setRateAndBufferSizeDetails(48000.0, 512);
        player.setProcessor(plugin.get());

        editor.reset(plugin->createEditorIfNeeded());

        if (editor != nullptr)
        {
            addAndMakeVisible(editor.get());
            editor->setResizable(true, true);
            status.setText("RUNNING • " + descriptions[0]->name,
                           juce::dontSendNotification);
            resized();
        }
        else
        {
            status.setText("Loaded • plugin has no custom editor",
                           juce::dontSendNotification);
        }

        repaint();
    }

    void closePlugin()
    {
        editor.reset();
        player.setProcessor(nullptr);
        plugin.reset();
        resized();
    }
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow("Kyoto VST3 Quick Builder",
                         juce::Colour(0xff0b0d10),
                         DocumentWindow::allButtons)
    {
        setContentOwned(new HostView(), true);
        setResizable(true, true);
        centreWithSize(1000, 720);
        setUsingNativeTitleBar(true);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class KyotoApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "Kyoto VST3 Quick Builder";
    }

    const juce::String getApplicationVersion() override
    {
        return "1.0.1";
    }

    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
    }

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(KyotoApp)
