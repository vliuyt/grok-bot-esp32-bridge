import base64
import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'firmware/tools'))
from fixture_audio import FixtureAudio, reuse_rounds


class FixtureAudioTest(unittest.TestCase):
    def test_long_fixture_drains_each_bounded_chunk(self):
        fixture = object.__new__(FixtureAudio)
        fixture.child = type('Child', (), {'stdin': io.StringIO()})()
        waits = []
        fixture._wait = lambda kind, timeout: waits.append(kind)
        pcm = bytes(range(250)) * 1000
        fixture.play(pcm)
        commands = [json.loads(line) for line in fixture.child.stdin.getvalue().splitlines()]
        chunks = [base64.b64decode(command['pcm']) for command in commands]
        self.assertTrue(all(0 < len(chunk) <= 96000 and len(chunk) % 2 == 0 for chunk in chunks))
        self.assertEqual(b''.join(chunks), pcm)
        self.assertEqual(waits, ['drained'] * len(chunks))

    def test_previous_rounds_require_identical_firmware(self):
        report = {'firmwareSha256': 'older', 'rounds': [{'passed': True}] * 10}
        with self.assertRaisesRegex(RuntimeError, 'FIRMWARE_MISMATCH'):
            reuse_rounds(report, 'current')
        self.assertEqual(report['firmwareSha256'], 'older')
        self.assertIs(reuse_rounds(report, 'older'), report)


if __name__ == '__main__':
    unittest.main()
