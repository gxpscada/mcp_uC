import pathlib
import struct
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from modbus_client import Client, crc16


class Port:
    def __init__(self, body, corrupt=False):
        self.body = body
        self.corrupt = corrupt

    def write(self, data):
        assert crc16(data) == 0
        self.request = data
        self.data = bytearray(self.body + struct.pack('<H', crc16(self.body)))
        if self.corrupt:
            self.data[-1] ^= 1

    def read(self, size):
        result = self.data[:size]
        del self.data[:size]
        return result


class Tests(unittest.TestCase):
    def test_crc(self):
        self.assertEqual(crc16(bytes.fromhex('01030000000a')), 0xcdc5)

    def test_fragmented_read(self):
        client = Client(Port(bytes.fromhex('0103041234abcd')))
        self.assertEqual(client.registers(3, 0, 2), [0x1234, 0xabcd])

    def test_bad_crc(self):
        with self.assertRaises(ValueError):
            Client(Port(bytes.fromhex('0103020003'), True)).registers(3, 0, 1)

    def test_exception(self):
        with self.assertRaisesRegex(RuntimeError, 'exception 2'):
            Client(Port(bytes.fromhex('018402'))).registers(4, 0x11, 1)

    def test_wrong_write_echo(self):
        with self.assertRaisesRegex(ValueError, 'acknowledgement'):
            Client(Port(bytes.fromhex('01050001ff00'))).coil(0, True)

    def test_signed_offset(self):
        port = Port(bytes.fromhex('011000090001'))
        Client(port).write(9, [(-2) & 0xffff])
        self.assertEqual(port.request[7:9], b'\xff\xfe')


if __name__ == '__main__':
    unittest.main()
