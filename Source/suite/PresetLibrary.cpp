#include "PresetLibrary.h"

namespace openglitch::suite {

namespace {
constexpr const char* kPresetTag = "openGlitchPreset";

juce::String favoriteKey(bool factory, const juce::String& name) {
    return (factory ? "F\t" : "U\t") + name;
}
} // namespace

PresetLibrary::PresetLibrary(juce::String pluginName, std::vector<FactoryPresetInfo> factories)
    : pluginName_(std::move(pluginName)), factories_(std::move(factories)) {}

void PresetLibrary::setDirectoryForTests(const juce::File& directory) {
    directoryOverride_ = directory;
}

juce::File PresetLibrary::directory() const {
    if (directoryOverride_ != juce::File()) {
        return directoryOverride_;
    }
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Alan Marcero")
        .getChildFile(pluginName_)
        .getChildFile("Presets");
}

bool PresetLibrary::legalName(const juce::String& name) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty() || trimmed.length() > 80) {
        return false;
    }
    if (trimmed.startsWithChar('.') || trimmed.endsWithChar('.') || trimmed.endsWithChar(' ')) {
        return false;
    }
    return trimmed.indexOfAnyOf("\\/:*?\"<>|") < 0 && trimmed == juce::File::createLegalFileName(trimmed);
}

bool PresetLibrary::isFactoryName(const juce::String& name) const {
    for (const auto& factory : factories_) {
        if (factory.name.equalsIgnoreCase(name.trim())) {
            return true;
        }
    }
    return false;
}

juce::File PresetLibrary::fileFor(const juce::String& name) const {
    return directory().getChildFile(name.trim() + ".xml");
}

juce::StringArray PresetLibrary::favoriteLines() const {
    const auto file = directory().getChildFile("favorites.txt");
    if (!file.existsAsFile()) {
        return {};
    }
    return juce::StringArray::fromLines(file.loadFileAsString());
}

void PresetLibrary::writeFavoriteLines(const juce::StringArray& lines) const {
    auto dir = directory();
    dir.createDirectory();
    dir.getChildFile("favorites.txt").replaceWithText(lines.joinIntoString("\n"));
}

bool PresetLibrary::isFavorite(const juce::String& name, bool factory) const {
    return favoriteLines().contains(favoriteKey(factory, name));
}

void PresetLibrary::setFavorite(const juce::String& name, bool factory, bool favorite) {
    if (name.isEmpty()) {
        return;
    }
    if (favorite && ((factory && !isFactoryName(name)) || (!factory && !fileFor(name).existsAsFile()))) {
        return;
    }
    auto lines = favoriteLines();
    const auto key = favoriteKey(factory, name);
    lines.removeString(key);
    if (favorite) {
        lines.add(key);
    }
    writeFavoriteLines(lines);
}

std::vector<PresetLibrary::Entry> PresetLibrary::entries(const juce::String& search) const {
    const auto query = search.trim();
    std::vector<Entry> result;
    auto add = [&](Entry entry) {
        if (query.isNotEmpty() && !entry.name.containsIgnoreCase(query) && !entry.group.containsIgnoreCase(query)) {
            return;
        }
        entry.favorite = isFavorite(entry.name, entry.factory);
        result.push_back(std::move(entry));
    };
    for (int index = 0; index < static_cast<int>(factories_.size()); ++index) {
        const auto& factory = factories_[static_cast<size_t>(index)];
        add({ factory.name, factory.group, true, false, index });
    }
    for (const auto& file : directory().findChildFiles(juce::File::findFiles, false, "*.xml")) {
        const auto name = file.getFileNameWithoutExtension();
        if (!isFactoryName(name)) {
            add({ name, "User", false, false, -1 });
        }
    }
    return result;
}

bool PresetLibrary::saveUser(const juce::String& name, const juce::ValueTree& state, juce::String& error) {
    const auto trimmed = name.trim();
    if (!legalName(trimmed)) {
        error = "Use a short name without path characters.";
        return false;
    }
    if (isFactoryName(trimmed)) {
        error = "Factory presets stay read-only.";
        return false;
    }
    auto dir = directory();
    if (!dir.createDirectory()) {
        error = "Could not create the preset folder.";
        return false;
    }
    juce::ValueTree root(kPresetTag);
    root.setProperty("name", trimmed, nullptr);
    root.appendChild(state.createCopy(), nullptr);
    const auto xml = root.createXml();
    if (xml == nullptr || !xml->writeTo(fileFor(trimmed))) {
        error = "Could not write the preset file.";
        return false;
    }
    error.clear();
    return true;
}

bool PresetLibrary::renameUser(const juce::String& from, const juce::String& to, juce::String& error) {
    const auto source = from.trim();
    const auto target = to.trim();
    if (isFactoryName(source)) {
        error = "Factory presets stay read-only.";
        return false;
    }
    if (!legalName(target) || isFactoryName(target)) {
        error = isFactoryName(target) ? "Factory presets stay read-only." : "Use a short name without path characters.";
        return false;
    }
    const auto sourceFile = fileFor(source);
    if (!sourceFile.existsAsFile()) {
        error = "That user preset is not on disk.";
        return false;
    }
    const auto targetFile = fileFor(target);
    if (targetFile != sourceFile && targetFile.existsAsFile()) {
        error = "A user preset with that name already exists.";
        return false;
    }
    if (!sourceFile.moveFileTo(targetFile)) {
        error = "Could not rename the preset file.";
        return false;
    }
    if (isFavorite(source, false)) {
        setFavorite(source, false, false);
        setFavorite(target, false, true);
    }
    error.clear();
    return true;
}

bool PresetLibrary::deleteUser(const juce::String& name, juce::String& error) {
    const auto trimmed = name.trim();
    if (isFactoryName(trimmed)) {
        error = "Factory presets stay read-only.";
        return false;
    }
    const auto file = fileFor(trimmed);
    if (!file.existsAsFile() || !file.deleteFile()) {
        error = "Could not delete the preset file.";
        return false;
    }
    setFavorite(trimmed, false, false);
    error.clear();
    return true;
}

juce::ValueTree PresetLibrary::loadUser(const juce::String& name) const {
    const auto file = fileFor(name);
    if (!file.existsAsFile() || isFactoryName(name)) {
        return {};
    }
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) {
        return {};
    }
    const auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.hasType(kPresetTag) || tree.getNumChildren() == 0) {
        return tree.hasType("OpenGlitchFXParameters") ? tree : juce::ValueTree();
    }
    return tree.getChild(0).createCopy();
}

} // namespace openglitch::suite
