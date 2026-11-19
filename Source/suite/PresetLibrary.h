#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace openglitch::suite {

struct FactoryPresetInfo {
    juce::String name;
    juce::String group;
};

// Factory presets are read-only. User presets are XML files in
// ~/Documents/Alan Marcero/<pluginName>/Presets.
class PresetLibrary {
public:
    struct Entry {
        juce::String name;
        juce::String group;
        bool factory = false;
        bool favorite = false;
        int factoryIndex = -1;
    };

    PresetLibrary(juce::String pluginName, std::vector<FactoryPresetInfo> factories);

    void setDirectoryForTests(const juce::File& directory);
    juce::File directory() const;

    std::vector<Entry> entries(const juce::String& search) const;
    bool isFavorite(const juce::String& name, bool factory) const;
    void setFavorite(const juce::String& name, bool factory, bool favorite);

    bool saveUser(const juce::String& name, const juce::ValueTree& state, juce::String& error);
    bool renameUser(const juce::String& from, const juce::String& to, juce::String& error);
    bool deleteUser(const juce::String& name, juce::String& error);
    juce::ValueTree loadUser(const juce::String& name) const;

    bool isFactoryName(const juce::String& name) const;

private:
    juce::File fileFor(const juce::String& name) const;
    juce::StringArray favoriteLines() const;
    void writeFavoriteLines(const juce::StringArray& lines) const;
    static bool legalName(const juce::String& name);

    juce::String pluginName_;
    std::vector<FactoryPresetInfo> factories_;
    juce::File directoryOverride_;
};

} // namespace openglitch::suite
