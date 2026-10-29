"""USB COM Modbus client. Requires pyserial. Example: COM5 sample."""
import argparse
import json
import struct
import time


def crc16(data):
    crc = 0xffff
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


class Client:
    def __init__(self, port, address=1):
        self.port, self.address = port, address

    def transact(self, function, payload):
        message = bytes([self.address, function]) + payload
        message += struct.pack('<H', crc16(message))
        self.port.write(message)
        result, expected = bytearray(), None
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            result.extend(self.port.read(1))
            if len(result) >= 2 and result[1] & 0x80:
                expected = 5
            elif len(result) >= 3:
                expected = result[2] + 5 if function in (1, 2, 3, 4) else 8
            if expected is not None and len(result) >= expected:
                break
        if expected is None or len(result) != expected:
            raise TimeoutError('Incomplete Modbus response')
        if crc16(result) or result[0] != self.address:
            raise ValueError('Bad CRC/address')
        if result[1] == function | 0x80:
            raise RuntimeError(f'Modbus exception {result[2]}')
        if result[1] != function:
            raise ValueError('Unexpected function')
        if function in (5, 6, 15, 16) and result[2:6] != message[2:6]:
            raise ValueError('Write acknowledgement mismatch')
        return result

    def registers(self, function, address, count):
        result = self.transact(function, struct.pack('>HH', address, count))
        if result[2] != count * 2:
            raise ValueError('Unexpected register count')
        return list(struct.unpack('>' + 'H' * count, result[3:-2]))

    def coil(self, address, value):
        self.transact(5, struct.pack('>HH', address, 0xff00 if value else 0))

    def write(self, address, values):
        payload = struct.pack('>HHB', address, len(values), len(values) * 2)
        payload += struct.pack('>' + 'H' * len(values), *values)
        self.transact(16, payload)

    def wait_command(self, address):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            reply = self.transact(1, struct.pack('>HH', address, 1))
            if not reply[3] & 1:
                error = self.registers(4, 0x1d, 1)[0]
                if error:
                    raise RuntimeError(f'Controller ErrorCode {error}')
                return
            time.sleep(0.02)
        raise TimeoutError('Deferred command did not finish')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('command', choices=['rtc', 'start', 'stop', 'sample', 'write', 'save', 'clear', 'defaults', 'reboot'])
    parser.add_argument('values', nargs='*', type=lambda x: int(x, 0))
    parser.add_argument('--address', type=int, default=1, choices=range(1, 248), metavar='1..247')
    args = parser.parse_args()
    import serial
    with serial.Serial(args.port, 921600, timeout=0.1, write_timeout=1) as port:
        client = Client(port, args.address)
        if args.command in ('start', 'stop'):
            client.coil(0, args.command == 'start')
        elif args.command == 'rtc':
            client.coil(0, False)
            epoch = int(time.time())
            client.write(0x1b, [epoch & 0xffff, epoch >> 16])
            client.coil(3, True)
            client.wait_command(3)
        elif args.command == 'write':
            if len(args.values) != 2 or not 0 <= args.values[0] <= 0x1c or not -32768 <= args.values[1] <= 65535:
                parser.error('write needs address 0..0x1c and a 16-bit value')
            client.write(args.values[0], [args.values[1] & 0xffff])
        elif args.command == 'sample':
            data = client.registers(4, 0x15, 19)
            print(json.dumps({'utc_seconds': data[9] | data[10] << 16,
                'error': data[8],
                'temperature_c': [None if x == 0x8000 else (x if x < 32768 else x - 65536) / 100 for x in data[:8]],
                'raw_adc': [None if x == 0xffff else x for x in data[11:]]}))
        else:
            client.coil(0, False)
            address = {'save': 1, 'clear': 2, 'defaults': 5, 'reboot': 4}[args.command]
            client.coil(address, True)
            if args.command != 'reboot':
                client.wait_command(address)


if __name__ == '__main__':
    main()
