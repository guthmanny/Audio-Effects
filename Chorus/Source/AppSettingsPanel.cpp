#include "AppSettingsPanel.h"

#include "AudioSettingsPanel.h"
#include "NoiseGateSettingsPanel.h"
#include "PluginProcessor.h"

namespace
{
constexpr int kNavItemHeight = 36;
constexpr int kSidebarPad = 12;
} // namespace

class AppSettingsPanel::NavItem final : public juce::Component
{
public:
    NavItem (const juce::String& titleIn, Page pageIn, AppSettingsPanel& ownerIn)
        : title (titleIn), page (pageIn), owner (ownerIn)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    Page getPage() const noexcept { return page; }

    void setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;
        selected = shouldBeSelected;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
        const auto bg = findColour (juce::ResizableWindow::backgroundColourId);
        const auto text = findColour (juce::Label::textColourId);

        if (selected || isMouseOver())
        {
            g.setColour (text.withAlpha (selected ? 0.18f : 0.08f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        g.setColour (text.withAlpha (selected ? 1.0f : 0.75f));
        g.setFont (AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(),
                                               selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (title, bounds.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            owner.selectPage (page);
    }

private:
    juce::String title;
    Page page;
    AppSettingsPanel& owner;
    bool selected = false;
};

AppSettingsPanel::AppSettingsPanel (juce::AudioDeviceManager& deviceManagerIn,
                                    ChorusAudioProcessor& processorIn,
                                    AtomLookAndFeel& lookAndFeel)
    : deviceManager (deviceManagerIn),
      processor (processorIn),
      atomLookAndFeel (lookAndFeel),
      sidebarTitle ("sidebarTitle", "Settings")
{
    setLookAndFeel (&atomLookAndFeel);

    sidebarTitle.setFont (AtomLookAndFeel::getUIFont (16.0f, juce::Font::bold));
    sidebarTitle.setJustificationType (juce::Justification::centredLeft);
    sidebar.addAndMakeVisible (sidebarTitle);

    addAndMakeVisible (sidebar);
    addAndMakeVisible (contentHost);

    audioPage = std::make_unique<AudioSettingsPanel> (deviceManager, atomLookAndFeel);
    noiseGatePage = std::make_unique<NoiseGateSettingsPanel> (processor, atomLookAndFeel);
    contentHost.addChildComponent (*audioPage);
    contentHost.addChildComponent (*noiseGatePage);

    rebuildNav();
    selectPage (Page::AudioSettings);
}

AppSettingsPanel::~AppSettingsPanel()
{
    setLookAndFeel (nullptr);
}

void AppSettingsPanel::rebuildNav()
{
    navItems.clear();

    auto addItem = [this] (const juce::String& title, Page page)
    {
        auto item = std::make_unique<NavItem> (title, page, *this);
        sidebar.addAndMakeVisible (*item);
        navItems.push_back (std::move (item));
    };

    addItem ("Audio Settings", Page::AudioSettings);
    addItem ("Noise Gate", Page::NoiseGate);
}

void AppSettingsPanel::selectPage (Page page)
{
    selectedPage = page;
    for (auto& item : navItems)
        item->setSelected (item->getPage() == page);
    showSelectedPage();
    resized();
}

void AppSettingsPanel::showSelectedPage()
{
    if (audioPage != nullptr)
        audioPage->setVisible (selectedPage == Page::AudioSettings);
    if (noiseGatePage != nullptr)
        noiseGatePage->setVisible (selectedPage == Page::NoiseGate);
}

int AppSettingsPanel::getMinimumWidth() const noexcept
{
    const int audioMin = audioPage != nullptr ? audioPage->getMinimumPanelWidth() : 420;
    return sidebarWidth + juce::jmax (420, audioMin);
}

int AppSettingsPanel::getMinimumHeight() const noexcept
{
    const int audioMin = audioPage != nullptr ? audioPage->getMinimumPanelHeight() : 360;
    return juce::jmax (360, juce::jmin (audioMin, 600));
}

void AppSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    auto sidebarBounds = getLocalBounds().removeFromLeft (sidebarWidth);
    g.setColour (findColour (juce::Label::textColourId).withAlpha (0.08f));
    g.fillRect (sidebarBounds);
    g.setColour (findColour (juce::Label::textColourId).withAlpha (0.18f));
    g.drawVerticalLine (sidebarWidth - 1, 0.0f, (float) getHeight());
}

void AppSettingsPanel::resized()
{
    auto bounds = getLocalBounds();
    sidebar.setBounds (bounds.removeFromLeft (sidebarWidth));
    contentHost.setBounds (bounds);

    auto side = sidebar.getLocalBounds().reduced (kSidebarPad);
    sidebarTitle.setBounds (side.removeFromTop (28));
    side.removeFromTop (10);

    for (auto& item : navItems)
    {
        item->setBounds (side.removeFromTop (kNavItemHeight));
        side.removeFromTop (4);
    }

    if (audioPage != nullptr)
        audioPage->setBounds (contentHost.getLocalBounds());
    if (noiseGatePage != nullptr)
        noiseGatePage->setBounds (contentHost.getLocalBounds());
}
