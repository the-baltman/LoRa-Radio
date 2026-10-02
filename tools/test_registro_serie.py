"""Unit tests for registro_serie.py. Run: python -m unittest tools/test_registro_serie.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import registro_serie as rs  # noqa: E402

SAMPLE = """\
LoRa-Radio fw 0.1.0 node 1
LOG,100,BOOT,1,0,0,0,0.0,0.00,1,200000,1,0
LOG,3000,TX,1,255,3,65534,0.0,0.00,8,0,0,0
LOG,3300,ACK,1,2,3,65534,-90.0,6.00,-92,650,1,0
LOG,6000,TX,1,255,3,65535,0.0,0.00,8,0,0,0
LOG,6300,ACK,1,2,3,65535,-91.0,5.00,-94,500,1,0
LOG,9000,TX,1,255,3,0,0.0,0.00,8,0,0,0
LOG,9600,FAIL,1,0,3,0,0.0,0.00,0,0,1,0
LOG,12000,TX,1,255,3,2,0.0,0.00,8,0,0,0
LOG,12300,ACK,1,2,3,2,-92.0,4.00,-95,400,1,0
LOG,15000,CLIPTX,1,2,8,7,-90.0,5.00,0,0,1,2650
garbage line
LOG,bad,line
LOG,500,RX,2,1,3,65534,-90.0,6.00,0,0,0,0
LOG,600,RX,2,1,3,0,-92.0,5.00,0,0,0,0
LOG,700,CLIPRX,2,1,8,7,-90.0,5.00,0,0,1,2500
"""


class T(unittest.TestCase):
    def test_parse(self):
        self.assertIsNone(rs.parse_line("hello"))
        self.assertIsNone(rs.parse_line("LOG,bad,line"))
        r = rs.parse_line("LOG,3300,ACK,1,2,3,65534,-90.0,6.00,-92,650,1,0")
        self.assertEqual((r["ev"], r["peer"], r["id"], r["rssi"], r["a"]), ("ACK", 2, 65534, -90.0, -92))

    def test_summary(self):
        rows = [r for r in map(rs.parse_line, SAMPLE.splitlines()) if r]
        text = "\n".join(rs.summarize(rows))
        self.assertIn("PRUEBA sent 4, ACKed 3, no ACK 1 -> round-trip delivery 75.0 %", text)
        # ids 65534 -> 0 skips 65535 across the wrap: 1 id skipped, 2 received
        self.assertIn("from node 1: 2 received, 1 ids skipped", text)
        self.assertIn("clips sent -> ok: 1", text)
        self.assertIn("clips received complete: 1", text)


if __name__ == "__main__":
    unittest.main()
