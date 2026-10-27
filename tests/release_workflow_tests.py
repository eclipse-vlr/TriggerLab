import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/publish-release.sh"

class RecoveryTests(unittest.TestCase):
    def test_resume_draft_and_preserve_published_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(TriggerLab VERSION 1.2.3 LANGUAGES CXX)\n")
            (root / "docs/releases").mkdir(parents=True)
            (root / "docs/releases/1.2.3.md").write_text("Notes\n")
            gh = root / "gh"
            gh.write_text("""#!/usr/bin/env python3
import hashlib, json, pathlib, sys
p=pathlib.Path('state.json')
state=json.loads(p.read_text()) if p.exists() else {}
args=sys.argv[1:]
if args[:2]==['release','view']:
    if not state: sys.exit(1)
    print(json.dumps(state))
elif args[:2]==['run','download']:
    d=pathlib.Path('release-assets'); d.mkdir(exist_ok=True)
    f=d/'TriggerLab-1.2.3-windows-x64.zip'; f.write_bytes(b'archive')
    (d/(f.name+'.sha256')).write_text(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\\n')
elif args[:2]==['release','create']:
    state={'isDraft': True, 'targetCommitish': 'commit'}
    p.write_text(json.dumps(state))
elif args[:2]==['release','upload']:
    if not pathlib.Path('failed-once').exists():
        pathlib.Path('failed-once').touch(); sys.exit(1)
    state['uploaded']=True; p.write_text(json.dumps(state))
elif args[:2]==['release','edit']:
    if not state.get('uploaded'): sys.exit(2)
    state['isDraft']=False; p.write_text(json.dumps(state))
else:
    sys.exit(2)
""")
            gh.chmod(0o755)
            env = dict(os.environ, PATH=str(root)+os.pathsep+os.environ["PATH"],
                       GITHUB_SHA="commit", GITHUB_RUN_ID="run")
            def run():
                return subprocess.run(["bash", str(SCRIPT)], cwd=root, env=env,
                                      capture_output=True, text=True)
            first = run()
            self.assertNotEqual(first.returncode, 0)
            self.assertTrue(json.loads((root/"state.json").read_text())["isDraft"])
            second = run()
            self.assertEqual(second.returncode, 0, second.stdout+second.stderr)
            published = (root/"state.json").read_text()
            self.assertFalse(json.loads(published)["isDraft"])
            self.assertEqual(run().returncode, 0)
            self.assertEqual((root/"state.json").read_text(), published)
            (root/"state.json").write_text(json.dumps({"isDraft": True, "targetCommitish": "other"}))
            self.assertNotEqual(run().returncode, 0)

if __name__ == "__main__":
    unittest.main()
