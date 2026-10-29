import importlib.util
from pathlib import Path
import tempfile
import unittest

script = Path(__file__).resolve().parents[1] / "scripts/verify-test-results.py"
spec = importlib.util.spec_from_file_location("test_results", script)
results = importlib.util.module_from_spec(spec)
spec.loader.exec_module(results)


class TestResultsTests(unittest.TestCase):
    def summarize(self, cases, log=""):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.xml"
            path.write_text("<testsuite>" + cases + "</testsuite>", encoding="utf-8")
            return results.summarize(path, log)

    def test_all_passed(self):
        actual = self.summarize('<testcase name="capture"/><testcase name="input"/>')
        self.assertEqual(actual["passed"], ["capture", "input"])
        self.assertFalse(actual["skipped"] or actual["failed"] or actual["internal_skips"])

    def test_external_skips_and_failures(self):
        actual = self.summarize('<testcase name="capture"><skipped/></testcase>'
                                '<testcase name="input"><failure>bad</failure></testcase>'
                                '<testcase name="disabled" status="notrun"/>')
        self.assertEqual(actual["skipped"], ["capture", "disabled"])
        self.assertEqual(actual["failed"], ["input"])

    def test_internal_skip_is_not_hidden_by_a_passing_exit_code(self):
        actual = self.summarize('<testcase name="first-run"/>',
                                "PASS: layout\nSKIP: F9 unavailable\nPASS: suite\n")
        self.assertEqual(actual["passed"], ["first-run"])
        self.assertEqual(actual["internal_skips"], ["SKIP: F9 unavailable"])


if __name__ == "__main__":
    unittest.main()
