Import("env")

from pathlib import Path


def parse_local_env(path):
    values = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if len(value) >= 2 and value[0] == value[-1] and value[0] in ('"', "'"):
            value = value[1:-1]

        values[key] = value

    return values


project_dir = Path(env["PROJECT_DIR"])
local_env = project_dir / "local.env"
values = parse_local_env(local_env)

github_token = values.get("PTC_GITHUB_TOKEN", "")
if github_token:
    env.Append(CPPDEFINES=[("PTC_GITHUB_TOKEN", github_token)])
