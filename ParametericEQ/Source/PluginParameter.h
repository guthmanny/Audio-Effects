/*
  ==============================================================================

    Code by Juan Gil <http://juangil.com/>.
    Copyright (C) 2017 Juan Gil.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

//==============================================================================

class PluginParametersManager
{
public:
    PluginParametersManager (AudioProcessor& p) : valueTreeState (p, nullptr)
    {
    }

    AudioProcessorValueTreeState valueTreeState;
    StringArray parameterTypes;
    Array<StringArray> comboBoxItemLists;
};

//==============================================================================

class PluginParameter
    : public LinearSmoothedValue<float>
    , public AudioProcessorValueTreeState::Listener
{
protected:
    PluginParameter (PluginParametersManager& manager,
                     std::function<float (float)> valueMapper = nullptr)
        : parametersManager (manager)
        , callback (std::move (valueMapper))
    {
    }

public:
    void updateValue (float value)
    {
        const auto mappedValue = callback != nullptr ? callback (value) : value;

        if (isSmoothing())
            setTargetValue (mappedValue);
        else
            setCurrentAndTargetValue (mappedValue);
    }

    void parameterChanged (const String&, float newValue) override
    {
        updateValue (newValue);
    }

    PluginParametersManager& parametersManager;
    std::function<float (float)> callback;
    String paramID;
};

//==============================================================================

class PluginParameterSlider : public PluginParameter
{
protected:
    PluginParameterSlider (PluginParametersManager& manager,
                           const String& parameterName,
                           const String& parameterLabelText,
                           float parameterMinValue,
                           float parameterMaxValue,
                           float parameterDefaultValue,
                           std::function<float (float)> valueMapper,
                           bool logarithmic)
        : PluginParameter (manager, std::move (valueMapper))
        , paramName (parameterName)
        , labelText (parameterLabelText)
        , minValue (parameterMinValue)
        , maxValue (parameterMaxValue)
        , defaultValue (parameterDefaultValue)
    {
        paramID = paramName.removeCharacters (" ").toLowerCase();
        parametersManager.parameterTypes.add ("Slider");

        NormalisableRange<float> range (minValue, maxValue);
        if (logarithmic)
            range.setSkewForCentre (sqrt (minValue * maxValue));

        parametersManager.valueTreeState.createAndAddParameter
            (paramID, paramName, labelText, range, defaultValue,
             [] (float mappedValue) { return String (mappedValue, 2); },
             [] (const String& text) { return text.getFloatValue(); });

        parametersManager.valueTreeState.addParameterListener (paramID, this);
        updateValue (defaultValue);
    }

public:
    String paramName;
    String labelText;
    float minValue;
    float maxValue;
    float defaultValue;
};

//======================================

class PluginParameterLinSlider : public PluginParameterSlider
{
public:
    PluginParameterLinSlider (PluginParametersManager& manager,
                              const String& parameterName,
                              const String& parameterLabelText,
                              float parameterMinValue,
                              float parameterMaxValue,
                              float parameterDefaultValue,
                              std::function<float (float)> valueMapper = nullptr)
        : PluginParameterSlider (manager,
                                 parameterName,
                                 parameterLabelText,
                                 parameterMinValue,
                                 parameterMaxValue,
                                 parameterDefaultValue,
                                 std::move (valueMapper),
                                 false)
    {
    }
};

//======================================

class PluginParameterLogSlider : public PluginParameterSlider
{
public:
    PluginParameterLogSlider (PluginParametersManager& manager,
                              const String& parameterName,
                              const String& parameterLabelText,
                              float parameterMinValue,
                              float parameterMaxValue,
                              float parameterDefaultValue,
                              std::function<float (float)> valueMapper = nullptr)
        : PluginParameterSlider (manager,
                                 parameterName,
                                 parameterLabelText,
                                 parameterMinValue,
                                 parameterMaxValue,
                                 parameterDefaultValue,
                                 std::move (valueMapper),
                                 true)
    {
    }
};

//==============================================================================

class PluginParameterToggle : public PluginParameter
{
public:
    PluginParameterToggle (PluginParametersManager& manager,
                           const String& parameterName,
                           bool initialState = false,
                           std::function<float (float)> valueMapper = nullptr)
        : PluginParameter (manager, std::move (valueMapper))
        , paramName (parameterName)
        , defaultState (initialState)
    {
        paramID = paramName.removeCharacters (" ").toLowerCase();
        parametersManager.parameterTypes.add ("ToggleButton");

        const StringArray toggleStates = {"False", "True"};
        NormalisableRange<float> range (0.0f, 1.0f, 1.0f);

        parametersManager.valueTreeState.createAndAddParameter
            (paramID, paramName, "", range, (float) defaultState,
             [toggleStates] (float mappedValue) { return toggleStates[(int) mappedValue]; },
             [toggleStates] (const String& text) { return toggleStates.indexOf (text); });

        parametersManager.valueTreeState.addParameterListener (paramID, this);
        updateValue ((float) defaultState);
    }

    String paramName;
    bool defaultState;
};

//==============================================================================

class PluginParameterComboBox : public PluginParameter
{
public:
    PluginParameterComboBox (PluginParametersManager& manager,
                             const String& parameterName,
                             StringArray parameterItems,
                             int initialChoice = 0,
                             std::function<float (float)> valueMapper = nullptr)
        : PluginParameter (manager, std::move (valueMapper))
        , paramName (parameterName)
        , items (std::move (parameterItems))
        , defaultChoice (initialChoice)
    {
        paramID = paramName.removeCharacters (" ").toLowerCase();
        parametersManager.parameterTypes.add ("ComboBox");

        parametersManager.comboBoxItemLists.add (items);
        NormalisableRange<float> range (0.0f, (float) items.size() - 1.0f, 1.0f);

        parametersManager.valueTreeState.createAndAddParameter
            (paramID, paramName, "", range, (float) defaultChoice,
             [this] (float mappedValue) { return items[(int) mappedValue]; },
             [this] (const String& text) { return items.indexOf (text); });

        parametersManager.valueTreeState.addParameterListener (paramID, this);
        updateValue ((float) defaultChoice);
    }

    String paramName;
    StringArray items;
    int defaultChoice;
};

//==============================================================================
