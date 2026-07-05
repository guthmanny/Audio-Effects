/*
  Custom Standalone entry point for Distortion.
  Uses the OS-native title bar, matching AtomTheme collections_app.
*/

#include "../JuceLibraryCode/JuceHeader.h"

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
void ensureEffectInputUnmuted (juce::StandalonePluginHolder& holder)
{
    // Distortion is an effect — it needs input audio. Always unmute, overriding
    // JUCE's feedback-loop default and any persisted "shouldMuteInput=true".
    holder.getMuteInputValue().setValue (false);
    if (auto* props = holder.settings.get())
        props->setValue ("shouldMuteInput", false);
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

        for (int i = getNumChildComponents(); --i >= 0;)
            if (auto* button = dynamic_cast<juce::TextButton*> (getChildComponent (i)))
                if (button->getButtonText() == "Options")
                    button->setVisible (false);

        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            ensureEffectInputUnmuted (*holder);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeStandaloneFilterWindow)
};

class DistortionStandaloneApp : public juce::JUCEApplication
{
public:
    DistortionStandaloneApp()
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
    return new DistortionStandaloneApp();
}
