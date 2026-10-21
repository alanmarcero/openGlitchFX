"""Include existing project and dependency notices in a release package."""
from pathlib import Path
import os
import shutil
import sys

package = Path(sys.argv[1])
package.mkdir(parents=True, exist_ok=True)
for pattern in ("README*", "LICENSE*", "NOTICE*", "THIRD_PARTY*"):
    for source in Path(".").glob(pattern):
        if source.is_file():
            shutil.copy2(source, package / source.name)

juce = Path("build/_deps/juce-src")
if not (juce / "LICENSE.md").is_file():
    raise SystemExit("JUCE license not found; refusing to package without notices")
for source in juce.rglob("*"):
    if source.is_file() and any(word in source.name.lower() for word in ("license", "licence", "copying", "notice", "copyright")):
        target = package / "licenses" / "JUCE" / source.relative_to(juce)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
# These dependencies keep their license text in a README or source header.
for relative in ("modules/juce_graphics/image_formats/jpglib/README", "modules/juce_core/zip/zlib/README", "modules/juce_opengl/opengl/juce_gl.h", "modules/juce_box2d/box2d/Box2D.h"):
    source = juce / relative
    if source.is_file():
        target = package / "licenses" / "JUCE" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
repository = os.environ.get("GITHUB_REPOSITORY", "")
revision = os.environ.get("GITHUB_SHA", "")
(package / "SOURCE.txt").write_text(
    f"Plugin source and build instructions: https://github.com/{repository}/tree/{revision}\n"
    "JUCE source and notices: https://github.com/juce-framework/JUCE/tree/8.0.9\n"
    "JUCE is dual licensed under AGPL-3.0 or a commercial JUCE license.\n"
    "Project licensing is described by the included project files.\n"
    "macOS bundles are ad hoc signed, not notarized.\n", encoding="utf-8")
