class BuilderJob> job;

    juce::ToggleButton buildMode;
    juce::Label modeLabel, status;
    juce::TextEditor logBox;
    juce::TextButton closeButton;
    bool dropActive = false;

    void updateMode()
    {
        bool b = buildMode.getToggleState();
        modeLabel.setText(b ? "BUILD → CACHE → LAUNCH" : "NATIVE VST3 PLAYER",
                           juce::dontSendNotification);
        status.setText(b ? "Drop a GitHub/CMake VST3 project here"
                         : "Drop a .vst3 folder here",
                       juce::dontSendNotification);
        repaint();
    }

    void appendLog(const juce::String& s)
    {
        juce::MessageManager::callAsync([this, s] {
            logBox.moveCaretToEnd();
            logBox.insertTextAtCaret(s + (s.endsWithChar('\n') ? "" : "\n"));
        });
    }

    void loadDroppedPlugin(juce::File file)
    {
        if (!file.isDirectory() || !file.hasFileExtension(".vst3")) {
            status.setText("Normal mode needs a .vst3 folder.", juce::dontSendNotification);
            return;
        }

        closePlugin();
        juce::AudioPluginFormat* vst3 = nullptr;
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
            if (formatManager.getFormat(i)->getName().containsIgnoreCase("VST3"))
                vst3 = formatManager.getFormat(i);

        if (vst3 == nullptr) {
            status.setText("VST3 format unavailable.", juce::dontSendNotification);
            return;
        }

        juce::OwnedArray<juce::PluginDescription> descs;
        if (!knownPlugins.scanAndAddFile(file, true, descs, *vst3) || descs.isEmpty()) {
            status.setText("Could not identify that VST3.", juce::dontSendNotification);
            return;
        }

        juce::String err;
        plugin = formatManager.createPluginInstance(*vst3, *descs[0], 48000.0, 512, err);
        if (plugin == nullptr) {
            status.setText("VST3 failed to load.", juce::dontSendNotification);
            appendLog(err);
            return;
        }

        plugin->setRateAndBufferSizeDetails(48000.0, 512);
        player.setProcessor(plugin.get());
        editor.reset(plugin->createEditorIfNeeded());

        if (editor != nullptr) {
            addAndMakeVisible(editor.get());
            editor->setResizable(true, true);
            status.setText("RUNNING • " + descs[0]->name, juce::dontSendNotification);
            resized();
        } else {
            status.setText("Loaded • plugin has no custom editor", juce::dontSendNotification);
        }
        repaint();
    }

    void closePlugin()
    {
        if (editor != nullptr) editor.reset();
        player.setProcessor(nullptr);
        plugin.reset();
        resized();
    }

    void startBuild(juce::File source)
    {
        if (!source.isDirectory()) {
            status.setText("Drop the extracted GitHub project folder, not a single source file.",
                           juce::dontSendNotification);
            return;
        }

        if (!source.getChildFile("CMakeLists.txt").exists()) {
            status.setText("No CMakeLists.txt found in the dropped folder.", juce::dontSendNotification);
            return;
        }

        closePlugin();
        logBox.clear();
        status.setText("Preparing fast incremental build…", juce::dontSendNotification);

        auto name = source.getFileNameWithoutExtension().replaceCharacters(" ", "_");
        auto cacheRoot = appData().getChildFile("Builds").getChildFile(name);
        cacheRoot.createDirectory();

        auto buildDir = cacheRoot.getChildFile("build");
        auto distDir = cacheRoot.getChildFile("dist");
        buildDir.createDirectory();
        distDir.createDirectory();

        auto stamp = source.getChildFile(".kyoto-build-stamp.txt");
        auto signature = makeSignature(source);
        auto existing = stamp.existsAsFile() ? stamp.loadFileAsString() : juce::String();

        if (existing == signature) {
            juce::Array<juce::File> cached;
            findVST3(distDir, cached);
            if (!cached.isEmpty()) {
                appendLog("CACHE HIT — skipping compilation.");
                status.setText("Cached build found • launching…", juce::dontSendNotification);
                loadDroppedPlugin(cached[0]);
                return;
            }
        }

        stamp.replaceWithText(signature);

        job = std::make_unique<BuilderJob>(source, cacheRoot,
            [this](const juce::String& s) { appendLog(s); },
            [this](bool ok, juce::File file, const juce::String& msg) {
                job.reset();
                status.setText(msg, juce::dontSendNotification);
                if (ok) {
                    appendLog("LAUNCHING " + file.getFullPathName());
                    loadDroppedPlugin(file);
                }
            });
        job->startThread();
    }

    static juce::String makeSignature(const juce::File& root)
    {
        juce::int64 totalSize = 0;
        juce::int64 newest = 0;
        int count = 0;
        juce::DirectoryIterator it(root, true);
        while (it.next()) {
            auto f = it.getFile();
            if (f.isDirectory()) continue;
            auto ext = f.getFileExtension().toLowerCase();
            if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" ||
                ext == ".cmake" || ext == ".txt" || f.getFileName() == "CMakeLists.txt") {
                ++count;
                totalSize += f.getSize();
                newest = juce::jmax(newest, f.getLastModificationTime().toMilliseconds());
            }
        }
        return juce::String(count) + ":" + juce::String(totalSize) + ":" + juce::String(newest);
    }

    static void findVST3(const juce::File& root, juce::Array<juce::File>& out)
    {
        juce::DirectoryIterator it(root, true, "*.vst3");
        while (it.next())
            if (it.getFile().isDirectory()) out.addIfNotAlreadyThere(it.getFile());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostView)
};

class BuilderJob final : public juce::Thread
{
public:
    BuilderJob(juce::File src, juce::File out,
               std::function<void(juce::String)> line,
               std::function<void(bool, juce::File, juce::String)> done)
        : Thread("VST3 Fast Build"), source(std::move(src)), output(std::move(out)),
          onLine(std::move(line)), onDone(std::move(done)) {}

    void run() override
    {
        auto buildDir = output.getChildFile("build");
        auto distDir = output.getChildFile("dist");
        buildDir.createDirectory();
        distDir.createDirectory();

        auto cmake = findCMake();
        if (cmake.isEmpty()) return finish(false, {}, "CMake was not found on this PC.");

        bool ninja = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getSiblingFile("ninja.exe").exists();
        auto ninjaOnPath = runCapture("where ninja.exe");
        if (ninjaOnPath.isNotEmpty()) ninja = true;

        juce::String configure = quote(cmake) + " -S " + quote(source.getFullPathName()) +
                                 " -B " + quote(buildDir.getFullPathName()) +
                                 " -DCMAKE_BUILD_TYPE=Release " +
                                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON";
        if (ninja) configure += " -G Ninja";
        else configure += " -G \"Visual Studio 17 2022\" -A x64";

        if (!execute(configure))
            return finish(false, {}, "BUILD ERROR • CMake configure failed.");

        juce::String build = quote(cmake) + " --build " + quote(buildDir.getFullPathName()) +
                             " --config Release --parallel";
        if (!execute(build))
            return finish(false, {}, "BUILD ERROR • compilation failed.");

        juce::Array<juce::File> bundles;
        findVST3(buildDir, bundles);
        if (bundles.isEmpty())
            return finish(false, {}, "BUILD ERROR • no .vst3 bundle was produced.");

        auto selected = bundles[0];
        auto target = distDir.getChildFile(selected.getFileName());
        target.deleteRecursively();
        if (!selected.copyDirectoryTo(target))
            return finish(false, {}, "BUILD ERROR • could not save the VST3 build.");

        finish(true, target, "BUILD COMPLETE • launching plugin…");
    }

private:
    juce::File source, output;
    std::function<void(juce::String)> onLine;
    std::function<void(bool, juce::File, juce::String)> onDone;

    static juce::String quote(const juce::String& s) { return "\"" + s + "\""; }

    juce::String findCMake()
    {
        auto r = runCapture("where cmake.exe");
        if (r.isNotEmpty()) return r.upToFirstOccurrenceOf("\r", false, false)
                                     .upToFirstOccurrenceOf("\n", false, false).trim();
        return {};
    }

    juce::String runCapture(const juce::String& command)
    {
        juce::ChildProcess p;
        if (!p.start(command)) return {};
        p.waitForProcessToFinish(10000);
        return p.readAllProcessOutput().trim();
    }

    bool execute(const juce::String& command)
    {
        onLine("\n> " + command + "\n");
        juce::ChildProcess p;
        if (!p.start(command)) {
            onLine("Could not start build command.\n");
            return false;
        }

        char buffer[8192];
        while (p.isRunning() && !threadShouldExit()) {
            auto bytes = p.readProcessOutput(buffer, (int) sizeof(buffer));
            if (bytes > 0)
                onLine(juce::String::fromUTF8(buffer, bytes));
            wait(8);
        }

        auto text = p.readAllProcessOutput();
        if (text.isNotEmpty()) onLine(text);
        return !threadShouldExit() && p.getExitCode() == 0;
    }

    static void findVST3(const juce::File& root, juce::Array<juce::File>& out)
    {
        juce::DirectoryIterator it(root, true, "*.vst3");
        while (it.next())
            if (it.getFile().isDirectory()) out.addIfNotAlreadyThere(it.getFile());
    }

    void finish(bool ok, juce::File f, juce::String message)
    {
        juce::MessageManager::callAsync([cb=onDone, ok, f, message] { cb(ok, f, message); });
    }
};


class HostView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    HostView()
    {
        setOpaque(true);
        formatManager.addDefaultFormats();
        audioDeviceManager.initialiseWithDefaultDevices(2, 2);
        audioDeviceManager.addAudioCallback(&player);

        buildMode.setButtonText("BUILD MODE");
        buildMode.setToggleState(true, juce::dontSendNotification);
        buildMode.onClick = [this] { updateMode(); };
        addAndMakeVisible(buildMode);

        status.setText("Drop a GitHub/CMake VST3 project here", juce::dontSendNotification);
        status.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(status);

        modeLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(modeLabel);

        logBox.setMultiLine(true);
        logBox.setReadOnly(true);
        logBox.setScrollbarsShown(true);
        logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff080a0d));
        logBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(logBox);

        closeButton.setButtonText("STOP / CLOSE");
        closeButton.onClick = [this] { closePlugin(); };
        addAndMakeVisible(closeButton);

        updateMode();
    }

    ~HostView() override
    {
        if (job != nullptr) {
            job->stopThread(2000);
            job.reset();
        }
        closePlugin();
        audioDeviceManager.removeAudioCallback(&player);
        audioDeviceManager.closeAudioDevice();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return !files.isEmpty();
    }

    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails&) override
    {
        dropActive = true; repaint();
    }
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override
    {
        dropActive = false; repaint();
    }
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        dropActive = false;
        if (details.files.isEmpty()) return;

        auto f = juce::File(details.files[0]);
        if (buildMode.getToggleState())
            startBuild(f);
        else
            loadDroppedPlugin(f);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101318));
        auto drop = getLocalBounds().reduced(20);
        drop.removeFromTop(85);
        drop.removeFromBottom(180);

        g.setColour(dropActive ? juce::Colour(0xff315b80) : juce::Colour(0xff1c232c));
        g.fillRoundedRectangle(drop.toFloat(), 18.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawRoundedRectangle(drop.toFloat(), 18.0f, 2.0f);

        g.setFont(25.0f);
        g.drawText(buildMode.getToggleState() ? "DROP VST3 SOURCE FOLDER" : "DROP A .VST3 FOLDER",
                   drop, juce::Justification::centredTop);
        g.setFont(15.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText(buildMode.getToggleState()
                       ? "CMake / JUCE / GitHub folders are supported • build is cached • successful builds launch automatically"
                       : "Loads the native VST3 editor and audio engine directly",
                   drop.reduced(20).withTrimmedTop(45),
                   juce::Justification::centredTop);

        if (dropActive) {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillRoundedRectangle(drop.reduced(6).toFloat(), 14.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        auto top = r.removeFromTop(64);
        buildMode.setBounds(top.removeFromLeft(150));
        modeLabel.setBounds(top.removeFromLeft(240));
        closeButton.setBounds(top.removeFromRight(140));

        auto bottom = r.removeFromBottom(165);
        logBox.setBounds(bottom);
        status.setBounds(r.removeFromBottom(38));

        if (editor != nullptr)
            editor->setBounds(r.reduced(5));
    }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioProcessorPlayer player;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow("Kyoto VST3 Quick Builder",
                         juce::Colour(0xff0b0d10), DocumentWindow::allButtons)
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

private:
};

class KyotoApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kyoto VST3 Quick Builder"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); }

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(KyotoApp)
