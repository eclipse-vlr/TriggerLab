import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/publish-release.sh"

FAKE_GH = r"""#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys
p = pathlib.Path("state.json")
state = json.loads(p.read_text()) if p.exists() else {}
tag_file = pathlib.Path("tags.json")
tags = json.loads(tag_file.read_text()) if tag_file.exists() else {}
args = sys.argv[1:]
with open("calls.jsonl", "a") as log:
    log.write(json.dumps(args) + "\n")
if args[:2] == ["release", "view"]:
    if not state:
        sys.exit(1)
    print(json.dumps(state))
elif args[:1] == ["api"]:
    if pathlib.Path("api-error").exists():
        sys.exit(1)
    endpoint = args[1]
    if "/git/matching-refs/tags/" in endpoint:
        prefix = endpoint.rsplit("/", 1)[-1]
        print(json.dumps([{"ref": "refs/tags/" + name, "object": value}
                          for name, value in tags.items() if name.startswith(prefix)]))
    elif "/git/tags/" in endpoint:
        object_id = endpoint.rsplit("/", 1)[-1]
        target = json.loads(pathlib.Path("annotated.json").read_text())[object_id]
        print(target["type"], target["sha"])
    else:
        sys.exit(2)
elif args[:2] == ["run", "download"]:
    d = pathlib.Path("release-assets")
    d.mkdir(exist_ok=True)
    f = d / "TriggerLab-1.2.3-windows-x64.zip"
    f.write_bytes(b"archive")
    (d / (f.name + ".sha256")).write_text(
        hashlib.sha256(f.read_bytes()).hexdigest() + "  " + f.name + "\n")
elif args[:2] == ["release", "create"]:
    state = {"isDraft": True, "targetCommitish": os.environ["GITHUB_SHA"]}
    p.write_text(json.dumps(state))
    tags["v1.2.3"] = {"type": "commit", "sha": os.environ["GITHUB_SHA"]}
    tag_file.write_text(json.dumps(tags))
elif args[:2] == ["release", "upload"]:
    if not pathlib.Path("failed-once").exists():
        pathlib.Path("failed-once").touch()
        sys.exit(1)
    state["uploaded"] = True
    p.write_text(json.dumps(state))
elif args[:2] == ["release", "edit"]:
    if not state.get("uploaded"):
        sys.exit(2)
    state["isDraft"] = False
    p.write_text(json.dumps(state))
else:
    sys.exit(2)
"""


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "CMakeLists.txt").write_text("project(TriggerLab VERSION 1.2.3 LANGUAGES CXX)\n")
        (self.root / "docs/releases").mkdir(parents=True)
        (self.root / "docs/releases/1.2.3.md").write_text("Notes\n")
        gh = self.root / "gh"
        gh.write_text(FAKE_GH)
        gh.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ["PATH"],
                        GITHUB_SHA="commit", GITHUB_RUN_ID="run", GH_REPO="test/TriggerLab")

    def write(self, name, value):
        (self.root / name).write_text(json.dumps(value))

    def run_release(self):
        return subprocess.run(["bash", str(SCRIPT)], cwd=self.root, env=self.env,
                              capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in (self.root / "calls.jsonl").read_text().splitlines()]

    def allow_upload(self):
        (self.root / "failed-once").touch()

    def assert_no_publication(self):
        self.assertFalse(any(call[:2] in [["release", "create"], ["release", "upload"],
                                          ["release", "edit"], ["run", "download"]]
                             for call in self.calls()))

    def test_resume_draft_and_preserve_published_release(self):
        first = self.run_release()
        self.assertNotEqual(first.returncode, 0)
        self.assertTrue(json.loads((self.root / "state.json").read_text())["isDraft"])
        second = self.run_release()
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        published = (self.root / "state.json").read_text()
        self.assertFalse(json.loads(published)["isDraft"])
        self.assertEqual(self.run_release().returncode, 0)
        self.assertEqual((self.root / "state.json").read_text(), published)

    def test_reject_other_draft_target(self):
        self.write("state.json", {"isDraft": True, "targetCommitish": "other"})
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assert_no_publication()

    def test_reject_wrong_lightweight_tag(self):
        self.write("tags.json", {"v1.2.3": {"type": "commit", "sha": "other"}})
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assert_no_publication()

    def test_matching_annotated_tag_can_be_published(self):
        self.write("tags.json", {"v1.2.3": {"type": "tag", "sha": "annotation"}})
        self.write("annotated.json", {"annotation": {"type": "commit", "sha": "commit"}})
        self.allow_upload()
        result = self.run_release()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reject_nested_annotated_tag_with_wrong_commit(self):
        self.write("tags.json", {"v1.2.3": {"type": "tag", "sha": "outer"}})
        self.write("annotated.json", {"outer": {"type": "tag", "sha": "inner"},
                                     "inner": {"type": "commit", "sha": "other"}})
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assert_no_publication()

    def test_prefix_match_is_not_the_release_tag(self):
        self.write("tags.json", {"v1.2.30": {"type": "commit", "sha": "other"}})
        self.allow_upload()
        result = self.run_release()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tag_lookup_api_failure_stops_publication(self):
        (self.root / "api-error").touch()
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assert_no_publication()

    def test_existing_published_release_is_untouched(self):
        state = {"isDraft": False, "targetCommitish": "older", "uploaded": True}
        self.write("state.json", state)
        self.write("tags.json", {"v1.2.3": {"type": "commit", "sha": "older"}})
        result = self.run_release()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads((self.root / "state.json").read_text()), state)
        self.assertEqual(self.calls(), [["release", "view", "v1.2.3",
                                        "--json", "isDraft,targetCommitish"]])


if __name__ == "__main__":
    unittest.main()
