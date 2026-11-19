"""Write release notes from a CHANGELOG section or an annotated tag message."""
import argparse
import os
import re
import subprocess
import sys

VERSION = re.compile(r"^v?(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)$")
HEADING = re.compile(
    r"^##\s+\[?v?(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)\]?(?:\s+.*)?$"
)
TRAILER = re.compile(
    r"^(Co-authored-by|Signed-off-by|Reviewed-by|Acked-by):", re.IGNORECASE
)


def version_of(tag):
    match = VERSION.fullmatch(tag.strip())
    if not match:
        raise SystemExit(f"unsupported tag name: {tag}")
    return match.group(1)


def changelog_notes(text, version):
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        match = HEADING.match(line.strip())
        if match and match.group(1) == version:
            start = index + 1
            break
    if start is None:
        return ""
    body = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        body.append(line.rstrip())
    return "\n".join(body).strip()


def clean_message(text):
    kept = [line.rstrip() for line in text.splitlines() if not TRAILER.match(line.strip())]
    return "\n".join(kept).strip()


def annotated_tag_message(tag):
    kind = subprocess.run(
        ["git", "cat-file", "-t", f"refs/tags/{tag}"],
        capture_output=True,
        text=True,
    )
    if kind.returncode != 0 or kind.stdout.strip() != "tag":
        return ""
    contents = subprocess.run(
        ["git", "for-each-ref", f"refs/tags/{tag}", "--format=%(contents)"],
        capture_output=True,
        text=True,
        check=True,
    )
    return clean_message(contents.stdout)


def previous_tag(tag):
    result = subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0", "--exclude", tag, f"{tag}^"],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        found = result.stdout.strip()
        return "" if not found or found == tag else found
    err = f"{result.stderr or ''}{result.stdout or ''}"
    if "No names found" in err or "No tags can describe" in err or "cannot describe" in err:
        return ""
    raise SystemExit((result.stderr or "git describe failed").strip())


def release_body(notes, repo, tag, previous):
    screenshot = (
        f"![Editor](https://raw.githubusercontent.com/{repo}/{tag}/docs/screenshots/preview.png)"
    )
    parts = [notes.strip(), "", screenshot]
    if previous:
        parts.extend(["", f"**Full Changelog**: https://github.com/{repo}/compare/{previous}...{tag}"])
    return "\n".join(parts) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG") or os.environ.get("GITHUB_REF_NAME", ""))
    parser.add_argument("--repo", default=os.environ.get("RELEASE_REPO") or os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--changelog", default="CHANGELOG.md")
    args = parser.parse_args()
    if not args.tag:
        raise SystemExit("missing tag")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
        raise SystemExit(f"unsupported repository: {args.repo}")

    version = version_of(args.tag)
    try:
        changelog = open(args.changelog, encoding="utf-8").read()
    except FileNotFoundError:
        changelog = ""
    notes = changelog_notes(changelog, version) or annotated_tag_message(args.tag)
    if not notes:
        raise SystemExit(
            f"No CHANGELOG section for {version} and no annotated tag message for {args.tag}"
        )
    sys.stdout.write(release_body(notes, args.repo, args.tag, previous_tag(args.tag)))


if __name__ == "__main__":
    main()
