"""wildpad.py reads and writes action text as test/actions.json says.

The setup page is checked against the same file by test/test_web.py, so the two
can't drift apart. Run: python test/test_actions.py
"""
import json
import sys
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import wildpad  # noqa: E402

V = json.loads((ROOT / "test" / "actions.json").read_text(encoding="utf-8"))


def seq_bytes(text):
    """Text -> the bytes its steps are stored as."""
    s = types.SimpleNamespace(seqs={})
    a = wildpad.Settings.parse(s, text)
    assert a[0] == wildpad.ACT_SEQ, text
    return wildpad.Settings._seq_bytes(s.seqs[a[1]])


class Actions(unittest.TestCase):
    def test_canonical(self):
        for text, a in V["canonical"]:
            with self.subTest(text=text):
                self.assertEqual(list(wildpad.parse_action(text)), a)
                self.assertEqual(wildpad.format_action(tuple(a)), text)

    def test_aliases(self):
        for text, a in V["aliases"]:
            with self.subTest(text=text):
                self.assertEqual(list(wildpad.parse_action(text)), a)

    def test_invalid(self):
        for text in V["invalid"]:
            with self.subTest(text=text), self.assertRaises(wildpad.ActionError):
                wildpad.parse_action(text)

    def test_sequences(self):
        for text, b in V["sequences"]:
            with self.subTest(text=text):
                self.assertEqual(seq_bytes(text), b)

    def test_invalid_sequences(self):
        for text in V["invalid_sequences"]:
            with self.subTest(text=text), self.assertRaises(wildpad.ActionError):
                seq_bytes(text)


if __name__ == "__main__":
    unittest.main()
