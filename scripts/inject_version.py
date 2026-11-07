Import("env")

import json
import subprocess
from urllib.parse import urlparse
from pathlib import Path


root = Path(env["PROJECT_DIR"])
version_path = root / "version.json"
generated_dir = root / ".pio" / "generated"
generated_header = generated_dir / "GeneratedVersion.h"

with version_path.open("r", encoding="utf-8") as handle:
    version = json.load(handle)

# Optionales Feld "label" (z. B. "1.5.A") kennzeichnet lokale Dev-Staende; Releases haben kein Label.
version_str = str(version.get("label") or f"{int(version['major'])}.{int(version['minor'])}.{int(version['commit'])}")


def parse_github_repo(remote_url: str):
    """Extract owner/name from common GitHub remote URL formats."""
    cleaned = (remote_url or "").strip()
    if not cleaned:
        return None, None

    if cleaned.startswith("git@github.com:"):
        slug = cleaned.split(":", 1)[1]
    elif cleaned.startswith("https://github.com/") or cleaned.startswith("http://github.com/"):
        slug = urlparse(cleaned).path.lstrip("/")
    elif cleaned.startswith("ssh://git@github.com/"):
        slug = urlparse(cleaned).path.lstrip("/")
    else:
        return None, None

    if slug.endswith(".git"):
        slug = slug[:-4]

    parts = [segment for segment in slug.split("/") if segment]
    if len(parts) < 2:
        return None, None

    return parts[0], parts[1]


repo_owner = "Back-code"
repo_name = "Level-Control"
try:
    remote_url = subprocess.check_output(
        ["git", "remote", "get-url", "origin"],
        cwd=str(root),
        text=True,
    ).strip()
    owner_from_remote, name_from_remote = parse_github_repo(remote_url)
    if owner_from_remote and name_from_remote:
        repo_owner = owner_from_remote
        repo_name = name_from_remote
except Exception:
    # Fall back to defaults if git is unavailable during build.
    pass

latest_release_api_url = f"https://api.github.com/repos/{repo_owner}/{repo_name}/releases/latest"
latest_release_url = f"https://github.com/{repo_owner}/{repo_name}/releases/latest"
release_tag_base_url = f"https://github.com/{repo_owner}/{repo_name}/releases/tag/v"

generated_dir.mkdir(parents=True, exist_ok=True)
generated_header.write_text(
    "#ifndef GENERATED_VERSION_H\n"
    "#define GENERATED_VERSION_H\n"
    f"#define LEVEL_CONTROL_VERSION \"{version_str}\"\n"
    f"#define LEVEL_CONTROL_GITHUB_OWNER \"{repo_owner}\"\n"
    f"#define LEVEL_CONTROL_GITHUB_REPO \"{repo_name}\"\n"
    f"#define LEVEL_CONTROL_LATEST_RELEASE_API_URL \"{latest_release_api_url}\"\n"
    f"#define LEVEL_CONTROL_LATEST_RELEASE_URL \"{latest_release_url}\"\n"
    f"#define LEVEL_CONTROL_RELEASE_TAG_BASE_URL \"{release_tag_base_url}\"\n"
    "#endif\n",
    encoding="utf-8",
)

env.Append(CPPPATH=[str(generated_dir)])
