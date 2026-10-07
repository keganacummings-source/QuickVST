#include <JuceHeader.h>

class AudioSource : public juce::AudioSource
{
public:
    void setPlugin(juce::AudioPluginInstance* p) { plugin = p; }
    void prepareToPlay(int samples, double rate) override { if (plugin) plugin->prepareToPlay(rate, samples); }
    void releaseResources() override { if (plugin) plugin->releaseResources(); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        if (!plugin) { info.clearActiveBufferRegion(); return; }
        plugin->processBlock(*info.buffer, midi);
        midi.clear();
    }
private:
    juce::AudioPluginInstance* plugin = nullptr;
    juce::MidiBuffer midi;
};

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
                      public juce::ListBoxModel
{
public:
    MainComponent()
    {
        setSize(1000, 680);
        title.setText("KYOTO VST3 QUICK HOST", juce::dontSendNotification);
        title.setFont(juce::FontOptions(22.0f).withStyle("bold"));
        addAndMakeVisible(title);

        status.setText("Drop a .vst3 plugin here, or choose one from the browser.",
                       juce::dontSendNotification);
        addAndMakeVisible(status);

        scanButton.setButtonText("Refresh");
        scanButton.onClick = [this] { scanVST3Folders(); };
        addAndMakeVisible(scanButton);

        openButton.setButtonText("Open VST3");
        openButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser>("Open VST3", {}, "*.vst3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& c)
            {
                auto f = c.getResult();
                if (f.exists()) loadPlugin(f);
            });
        };
        addAndMakeVisible(openButton);

        browser.setModel(this);
        browser.setRowHeight(42);
        addAndMakeVisible(browser);

        formatManager.addDefaultFormats();
        setAudioChannels(0, 2);
        loadCache();
        scanVST3Folders();
    }

    ~MainComponent() override
    {
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
        if (f.hasFileExtension("vst3")) { loadPlugin(f); return; }
        if (f.isDirectory())
        {
            auto found = f.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3");
            if (!found.isEmpty()) loadPlugin(found[0]);
        }
    }

    int getNumRows() override { return plugins.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) g.fillAll(juce::Colours::darkgrey.withAlpha(0.3f));
        if (row >= plugins.size()) return;
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(plugins[row].getFileNameWithoutExtension(), 12, 3, w - 24, 20,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(plugins[row].getFullPathName(), 12, 23, w - 24, 16,
                   juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < plugins.size()) loadPlugin(plugins[row]);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(14);
        title.setBounds(r.removeFromTop(34));
        auto bar = r.removeFromTop(42);
        openButton.setBounds(bar.removeFromRight(115));
        scanButton.setBounds(bar.removeFromRight(95).reduced(4, 0));
        status.setBounds(bar.reduced(6, 0));
        browser.setBounds(r);
    }

    void prepareToPlay(int samples, double rate) override { source.prepareToPlay(samples, rate); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override { source.getNextAudioBlock(info); }
    void releaseResources() override { source.releaseResources(); }

private:
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow(juce::AudioProcessorEditor& e, const juce::String& name)
            : DocumentWindow(name, juce::Colours::black, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(&e, true);
            setResizable(true, true);
            centreWithSize(900, 600);
            setVisible(true);
        }
        void closeButtonPressed() override { setVisible(false); }
    };

    void loadCache()
    {
        auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Kyoto/VST3QuickHost/plugins.txt");
        if (!f.existsAsFile()) return;
        juce::StringArray lines;
        lines.addLines(f.loadFileAsString());
        for (auto& line : lines)
        {
            juce::File p(line.trim());
            if (p.exists() && p.hasFileExtension("vst3")) plugins.addIfNotAlreadyThere(p);
        }
        browser.updateContent();
        status.setText("Loaded cached browser: " + juce::String(plugins.size()) + " VST3 plugin(s).",
                       juce::dontSendNotification);
    }

    void scanVST3Folders()
    {
        const juce::File roots[] = {
            juce::File("C:\\Program Files\\Common Files\\VST3"),
            juce::File("C:\\Program Files (x86)\\Common Files\\VST3")
        };
        for (auto& root : roots)
            if (root.isDirectory())
                for (auto& f : root.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3"))
                    plugins.addIfNotAlreadyThere(f);

        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("Kyoto/VST3QuickHost");
        dir.createDirectory();
        juce::StringArray lines;
        for (auto& p : plugins) lines.add(p.getFullPathName());
        dir.getChildFile("plugins.txt").replaceWithText(lines.joinIntoString("\n"));
        browser.updateContent();
        status.setText("Ready — " + juce::String(plugins.size()) + " VST3 plugin(s).",
                       juce::dontSendNotification);
    }

    void closePlugin()
    {
        source.setPlugin(nullptr);
        plugin.reset();
        editorWindow.reset();
        editor.reset();
    }

    void loadPlugin(const juce::File& file)
    {
        closePlugin();
        auto* format = formatManager.getFormat(0);
        if (!format) return;

        juce::KnownPluginList list;
        juce::PluginDirectoryScanner scanner(
            list, *format,
            juce::FileSearchPath(file.getParentDirectory().getFullPathName()),
            true, juce::File());

        juce::String dead;
        while (scanner.scanNextFile(true, dead)) {}

        const auto types = list.getTypes();
        const juce::PluginDescription* match = nullptr;
        for (const auto& d : types)
            if (juce::File(d.fileOrIdentifier) == file) { match = &d; break; }
        if (!match && !types.isEmpty()) match = &types.getReference(0);

        if (!match)
        {
            status.setText("Could not identify this VST3.", juce::dontSendNotification);
            return;
        }

        juce::String error;
        plugin = formatManager.createPluginInstance(
            *match, deviceManager.getAudioDeviceSetup().sampleRate, 512, error);

        if (!plugin)
        {
            status.setText("Plugin failed to load: " + error, juce::dontSendNotification);
            return;
        }

        source.setPlugin(plugin.get());
        editor.reset(plugin->createEditorIfNeeded());
        if (editor)
        {
            editorWindow = std::make_unique<PluginWindow>(*editor, match->name);
            editorWindow->setSize(900, 600);
        }
        status.setText("Loaded: " + match->name, juce::dontSendNotification);
    }

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<PluginWindow> editorWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    AudioSource source;
    juce::Label title, status;
    juce::TextButton scanButton, openButton;
    juce::ListBox browser;
    juce::Array<juce::File> plugins;
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : DocumentWindow("Kyoto VST3 Quick Host", juce::Colours::black, allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        centreWithSize(1000, 680);
        setResizable(true, true);
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kyoto VST3 Quick Host"; }
    const juce::String getApplicationVersion() override { return "1.0.1"; }
    void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); }
private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(App)
