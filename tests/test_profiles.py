"""Regression checks for reviewed profile generation and candidate rejection."""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from pe_study import self_test

from profiles import generate


class ProfileTests(unittest.TestCase):
    def test_generated_header_matches_reviewed_json(self) -> None:
        config = json.loads((ROOT / "profiles/profiles.json").read_text(encoding="utf-8"))
        self.assertEqual(
            generate(config),
            (ROOT / "src/profiles.generated.hpp").read_text(encoding="utf-8"),
        )

    def test_unreviewed_candidate_cannot_generate(self) -> None:
        with self.assertRaises(ValueError):
            generate({"review_status": "CANDIDATE_REQUIRES_REVIEW"})

    def test_signature_scanner_boundaries(self) -> None:
        self_test()


if __name__ == "__main__":
    unittest.main()
