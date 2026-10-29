# SPDX-License-Identifier: GPL-3.0-only
"""Independent archive decoding checks for interrupted/old A/B song records."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec=importlib.util.spec_from_file_location('snapshot',Path(__file__).resolve().parents[1]/'tools/fm1_snapshot.py')
snapshot=importlib.util.module_from_spec(spec);spec.loader.exec_module(snapshot)

class StorageArchive(unittest.TestCase):
    def setUp(self):self.raw=bytearray(b'\xff'*(snapshot.FLASH_HI-snapshot.FLASH_LO))
    def put(self,obj,copy,gen,payload):
        base,extra=snapshot.sectors(obj,copy)
        head=struct.pack('<IHH5I',0x554C4546,obj,copy,gen,len(payload),zlib.crc32(payload),0xffffffff,0xffffffff)
        head+=struct.pack('<I',zlib.crc32(head));start=base-snapshot.FLASH_LO
        self.raw[start:start+32]=head
        self.raw[start+256:start+256+min(3840,len(payload))]=payload[:3840]
        if len(payload)>3840:
            self.raw[extra-snapshot.FLASH_LO:extra-snapshot.FLASH_LO+len(payload)-3840]=payload[3840:]
    def test_extended_newest(self):
        self.put(32,0,4,b'a'*6400);self.put(32,1,5,b'b'*6400)
        self.assertEqual(snapshot.decode_object(self.raw,32)['payload'],b'b'*6400)
    def test_corrupt_newest_falls_back(self):
        self.put(43,0,4,b'a'*6400);self.put(43,1,5,b'b'*6400)
        self.raw[snapshot.sectors(43,1)[1]-snapshot.FLASH_LO]^=1
        self.assertEqual(snapshot.decode_object(self.raw,43)['copy'],0)
    def test_header_cut_falls_back(self):
        self.put(9,0,4,b'a'*6400);self.put(9,1,5,b'b'*6400)
        self.raw[snapshot.sectors(9,1)[0]-snapshot.FLASH_LO+31]^=1
        self.assertEqual(snapshot.decode_object(self.raw,9)['copy'],0)
    def test_generation_wrap_and_tie(self):
        self.put(32,0,0xffffffff,b'a');self.put(32,1,0,b'b')
        self.assertEqual(snapshot.decode_object(self.raw,32)['copy'],1)
        self.put(32,0,0,b'a');self.assertEqual(snapshot.decode_object(self.raw,32)['copy'],0)
    def test_empty_and_missing(self):
        self.assertIsNone(snapshot.decode_object(self.raw,1))
        self.put(1,0,1,b'');self.assertEqual(snapshot.decode_object(self.raw,1)['payload'],b'')
    def test_small_settings_and_legacy(self):
        self.put(0,0,1,b's'*572);self.put(33,0,1,b'p'*3840)
        self.assertEqual(snapshot.decode_object(self.raw,0)['length'],572)
        self.assertEqual(snapshot.decode_object(self.raw,33)['payload'],b'p'*3840)
    def test_extensions_are_disjoint(self):
        ids=[1,2,3,4,9]+list(range(32,44))
        addresses=[snapshot.sectors(i,c)[1] for i in ids for c in (0,1)]
        self.assertEqual(len(set(addresses)),34)
        self.assertEqual(min(addresses),0xB8000);self.assertEqual(max(addresses),0xD9000)
    def test_truncated_snapshot_refused(self):
        with self.assertRaises(ValueError):snapshot.decode_object(b'',32)
if __name__=='__main__':unittest.main()
