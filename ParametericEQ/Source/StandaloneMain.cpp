/*
  Custom Standalone entry point for ParametricEQ.
  Uses the OS-native title bar, matching AtomTheme collections_app.
*/

#include "../JuceLibraryCode/JuceHeader.h"

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
void ensureEffectInputUnmuted (juce::StandalonePluginHolder& holder)
{
    // ParametricEQ is an effect — it needs input audio. Always unmute, overriding
    // JUCE's feedback-loop default and any persisted "shouldMuteInput=true".
    holder.getMuteInputValue().setValue (false);

    if (auto* props = holder.settings.get())
        props->setValue ("shouldMuteInput", false);
}

void restartCurrentAudioDevice (juce::AudioDeviceManager& deviceManager)
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);

    if (setup.inputDeviceName.isEmpty() && setup.outputDeviceName.isEmpty())
        return;

    deviceManager.setAudioDeviceSetup (setup, false);
}

class NativeStandaloneFilterWindow : public juce::StandaloneFilterWindow
{
public:
    NativeStandaloneFilterWindow (const juce::String& title,
                                  juce::Colour backgroundColour,
                                  juce::PropertySet* settingsToUse,
                                  bool takeOwnershipOfSettings,
                                  const juce::String& preferredDefaultDeviceName,
                                  const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions,
                                  const juce::Array<juce::StandalonePluginHolder::PluginInOuts>& constrainToConfiguration,
                                  bool autoOpenMidiDevices = false)
        : StandaloneFilterWindow (title,
                                  backgroundColour,
                                  settingsToUse,
                                  takeOwnershipOfSettings,
                                  preferredDefaultDeviceName,
                                  preferredSetupOptions,
                                  constrainToConfiguration,
                                  autoOpenMidiDevices)
    {
        setUsingNativeTitleBar (true);
        atom::setNativeTitleBarDarkMode (*this);

        // The window background colour was fixed at construction time (before the
        // editor existed), so it used the default LnF colour rather than the Atom
        // theme. Re-sync it now that the editor has installed AtomLookAndFeel as
        // the global default, so the window background and the editor background
        // match during resize (no flash of a different colour at the edges).
        setBackgroundColour (juce::LookAndFeel::getDefaultLookAndFeel()
                                 .findColour (juce::ResizableWindow::backgroundColourId));

        for (int i = getNumChildComponents(); --i >= 0;)
            if (auto* button = dynamic_cast<juce::TextButton*> (getChildComponent (i)))
                if (button->getButtonText() == "Options")
                    button->setVisible (false);

        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            ensureEffectInputUnmuted (*holder);

            // ASIO often needs a device reopen before callbacks deliver audio on cold start.
            juce::Component::SafePointer<NativeStandaloneFilterWindow> safeWindow (this);
            juce::Timer::callAfterDelay (300, [safeWindow]()
            {
                if (safeWindow == nullptr)
                    return;

               #if JUCE_WINDOWS
                auto& deviceManager = safeWindow->getDeviceManager();

                if (deviceManager.getCurrentAudioDeviceType() == "ASIO")
                    restartCurrentAudioDevice (deviceManager);
               #endif
            });
        }
    }

    void resized() override
    {
        // Snap before laying out content so a WM-driven stretch cannot propagate
        // into MainContentComponent and stretch the plugin editor.
        syncFixedWindowSize();
        StandaloneFilterWindow::resized();
    }

private:
    void syncFixedWindowSize()
    {
        if (isSyncingSize)
            return;

        auto* content = getContentComponent();
        if (content == nullptr)
            return;

        const auto borders = getContentComponentBorder();
        const int expectedW = content->getWidth() + borders.getLeftAndRight();
        const int expectedH = content->getHeight() + borders.getTopAndBottom();

        if (expectedW <= 0 || expectedH <= 0)
            return;

        if (auto* c = getConstrainer())
            c->setSizeLimits (expectedW, expectedH, expectedW, expectedH);

        if (getWidth() == expectedW && getHeight() == expectedH)
        {
            lastStableBounds = getScreenBounds();
            return;
        }

        // WM resize can leave getBounds() at (0, 0) during resized(); use the last
        // known good screen position instead of the current one when snapping back.
        auto target = (lastStableBounds.isEmpty() ? getScreenBounds() : lastStableBounds)
                          .withSize (expectedW, expectedH);

        const juce::ScopedValueSetter<bool> scope (isSyncingSize, true);
        setBounds (target);
        lastStableBounds = getScreenBounds();
    }

    juce::Rectangle<int> lastStableBounds;
    bool isSyncingSize = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeStandaloneFilterWindow)
};

class ParametricEQStandaloneApp : public juce::JUCEApplication
{
public:
    ParametricEQStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override              { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                    { return true; }
    void anotherInstanceStarted (const juce::String&) override    {}

    void initialise (const juce::String&) override
    {
        mainWindow.reset (createWindow());
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->getPluginHolder()->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []()
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::StandaloneFilterWindow* createWindow()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        juce::StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
       #endif

        return new NativeStandaloneFilterWindow (getApplicationName(),
                                                 juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                                                 appProperties.getUserSettings(),
                                                 false,
                                                 {},
                                                 nullptr
                                                #ifdef JucePlugin_PreferredChannelConfigurations
                                                 , juce::Array<juce::StandalonePluginHolder::PluginInOuts> (channels, juce::numElementsInArray (channels))
                                                #else
                                                 , {}
                                                #endif
                                                #if JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                                                 , false
                                                #endif
                                                 );
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new ParametricEQStandaloneApp();
}
