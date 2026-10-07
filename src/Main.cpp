#include <JuceHeader.h>

class HostAudioSource final : public juce::AudioSource
{
public:
    void setPlugin(juce::AudioPluginInstance* p)
    {
        juce::ScopedLock sl(lock);
        plugin = p;
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        juce::ScopedLock sl(lock);
        currentSampleRate = sampleRate;
        blockSize = samplesPerBlockExpected;
        if (plugin)
            plugin->prepareToPlay(sampleRate, samplesPerBlockExpected);
    }

    void releaseResources() override
    {
        juce::ScopedLock sl(lock);
        if (plugin)
            plugin->releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        juce::ScopedLock sl(lock);
        if (!plugin)
        {
            info.clearActiveBufferRegion();
            return;
        }

        const int n = info.numSamples;
        plugin->processBlock(*info.buffer, midi);

        // Keep host audio stable if a plugin exposes fewer output channels.
        for (int ch = plugin->getTotalNumOutputChannels();
             ch < info.buffer->getNumChannels(); ++ch)
            info.buffer->clear(ch, info.startSample, n);

        midi.clear();
    }

private:
    juce::CriticalSection lock;
    juce::AudioPluginInstance* plugin = nullptr;
    juce::MidiBuffer midi;
    double currentSampleRate = 44100.0;
    int blockSize = 512;
};

class MainComponent final : public juce::AudioAppComponent,
                             public juce::FileDragAndDropTarget,
                             public juce::Timer
{
public:
    MainComponent()
    {
        setSize(1050, 700);

        addAndMakeVisible(title);
        title.setText("KYOTO VST3 QUICK HOST", juce::dontSendNotification);
        title.setFont(juce::FontOptions(22.0f).withStyle("bold"));

        addAndMakeVisible(status);
        status.setText("Drop a .vst3 folder anywhere in this window, or choose a plugin.", juce::dontSendNotification);

        addAndMakeVisible(scanButton);
        scanButton.setButtonText("Refresh VST3");
        scanButton.onClick = [this] { startScan(); };

        addAndMakeVisible(openButton);
        openButton.setButtonText("Open .VST3");
        openButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser>(
                "Choose a VST3 plugin",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*.vst3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& c)
            {
                auto f = c.getResult();
                if (f.exists())
                    loadPlugin(f);
            });
        };

        addAndMakeVisible(browser);
        browser.setMultiSelectEnabled(false);
        browser.setRowHeight(34);
        browser.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        browser.setModel(this);

        formatManager.addDefaultFormats();
        setAudioChannels(0, 2);

        scanCache();
        startTimerHz(4);
    }

    ~MainComponent() override
    {
        stopTimer();
        shutdownAudio();
        closePlugin();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto f : files)
            if (f.endsWithIgnoreCase(".vst3") || juce::File(f).isDirectory())
                return true;
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.isEmpty()) return;
        juce::File f(files[0]);

        // A VST3 is a directory bundle on Windows. Accept either the bundle
        // itself or a containing folder dropped by the user.
        if (f.isDirectory() && f.hasFileExtension("vst3"))
            loadPlugin(f);
        else if (f.isDirectory())
        {
            auto candidates = f.findChildFiles(juce::File::findFilesAndDirectories,
                                                true, "*.vst3");
            if (!candidates.isEmpty())
                loadPlugin(candidates[0]);
        }
    }

    int getNumRows() override { return plugins.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected) g.fillAll(juce::Colours::darkgrey.withAlpha(0.25f));
        if (row >= plugins.size()) return;

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(plugins[row].getFileNameWithoutExtension(),
                   12, 0, width - 24, height, juce::Justification::centredLeft);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(plugins[row].getFullPathName(),
                   12, height - 14, width - 24, 12, juce::Justification::centredLeft);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < plugins.size())
            loadPlugin(plugins[row]);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(14);
        title.setBounds(r.removeFromTop(34));
        auto top = r.removeFromTop(42);
        scanButton.setBounds(top.removeFromRight(130));
        openButton.setBounds(top.removeFromRight(120).reduced(4, 0));
        status.setBounds(top.reduced(6, 0));
        browser.setBounds(r);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        source.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        source.getNextAudioBlock(info);
    }

    void releaseResources() override
    {
        source.releaseResources();
    }

    void timerCallback() override
    {
        if (scanPending && !scanThread.isThreadRunning())
        {
            scanPending = false;
            browser.updateContent();
            status.setText("Ready — drop a plugin or double-click one from the browser.",
                           juce::dontSendNotification);
        }
    }

private:
    void scanCache()
    {
        auto cache = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("Kyoto")
                         .getChildFile("VST3QuickHost")
                         .getChildFile("plugins.txt");

        if (cache.existsAsFile())
        {
            juce::StringArray lines;
            lines.addLines(cache.loadFileAsString());
            for (auto& line : lines)
            {
                juce::File f(line.trim());
                if (f.exists() && f.hasFileExtension("vst3"))
                    plugins.addIfNotAlreadyThere(f);
            }
            browser.updateContent();
        }

        // Cache makes the UI instant; a background refresh keeps it current.
        startScan();
    }

    void startScan()
    {
        if (scanThread.isThreadRunning())
            return;

        scanPending = true;
        status.setText("Refreshing VST3 index in the background…", juce::dontSendNotification);

        scanThread.startThread();
        scanThread.stopThread(1);

        // Small, bounded filesystem scan. It does not instantiate plugins.
        juce::Array<juce::File> roots;
        roots.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
        roots.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));

        juce::Array<juce::File> found;
        for (auto& root : roots)
            if (root.isDirectory())
                found.addArray(root.findChildFiles(juce::File::findFilesAndDirectories,
                                                   true, "*.vst3").getRawDataPointer(),
                               root.findChildFiles(juce::File::findFilesAndDirectories,
                                                   true, "*.vst3").size());

        for (auto& f : found)
            plugins.addIfNotAlreadyThere(f);

        saveCache();
        scanThread.stopThread(1);
        scanPending = false;
        browser.updateContent();
        status.setText("Ready — " + juce::String(plugins.size()) + " VST3 plugin(s) indexed.",
                       juce::dontSendNotification);
    }

    void saveCache()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("Kyoto")
                       .getChildFile("VST3QuickHost");
        dir.createDirectory();

        juce::StringArray lines;
        for (auto& p : plugins)
            lines.add(p.getFullPathName());

        dir.getChildFile("plugins.txt").replaceWithText(lines.joinIntoString("\n"));
    }

    void closePlugin()
    {
        source.setPlugin(nullptr);
        plugin.reset();
        editor.reset();
    }

    void loadPlugin(const juce::File& file)
    {
        if (!file.exists()) return;

        closePlugin();

        juce::PluginDescription desc;
        juce::OwnedArray<juce::PluginDescription> found;
        formatManager.scanAndAddFile(file.getFullPathName(), true, found, scanner);

        if (found.isEmpty())
        {
            status.setText("Could not identify that VST3 plugin.", juce::dontSendNotification);
            return;
        }

        juce::String error;
        plugin.reset(formatManager.createPluginInstance(*found[0],
                                                        deviceManager.getAudioDeviceSetup()
                                                            .sampleRate,
                                                        512, error).release());

        if (plugin == nullptr)
        {
            status.setText("Plugin failed to load: " + error, juce::dontSendNotification);
            return;
        }

        source.setPlugin(plugin.get());

        // Open the plugin's native editor inside the host.
        editor.reset(plugin->createEditorIfNeeded());
        if (editor)
        {
            editor->setSize(900, 600);
            editorWindow = std::make_unique<PluginWindow>(*editor, found[0]->name);
        }

        status.setText("Loaded: " + found[0]->name, juce::dontSendNotification);
    }

    class PluginWindow final : public juce::DocumentWindow
    {
    public:
        PluginWindow(juce::AudioProcessorEditor& e, const juce::String& name)
            : DocumentWindow(name, juce::Colours::black,
                             DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(&e, true);
            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override { setVisible(false); }
    };

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::AudioPluginInstance::PluginDescription scanner;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<PluginWindow> editorWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    HostAudioSource source;
    juce::AudioProcessorGraph graph;

    juce::Label title, status;
    juce::TextButton scanButton, openButton;
    juce::ListBox browser;
    juce::Array<juce::File> plugins;
    juce::Thread scanThread{"VST3 index refresh"};
    bool scanPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow() : DocumentWindow("Kyoto VST3 Quick Host",
                                  juce::Colours::black,
                                  DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        centreWithSize(getWidth(), getHeight());
        setResizable(true, true);
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class KyotoApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kyoto VST3 Quick Host"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override { mainWindow.reset(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(KyotoApp)
