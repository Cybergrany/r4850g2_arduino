"""Exercise the production objdump parser, including the Mega frame regression."""
import contextlib
import io
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch


TOOL = Path(__file__).resolve().parents[1] / 'tools/analyze_avr_stack.py'


def analyze(disassembly):
    with patch('sys.argv', [str(TOOL), 'unused.elf']), \
            patch('subprocess.check_output', return_value=disassembly), \
            contextlib.redirect_stdout(io.StringIO()):
        return runpy.run_path(str(TOOL))


def frame(low, high='r1', opcode='subi'):
    adjustment = (f'  108: ca 56        subi r28, {low}\n'
                  f'  10a: d1 09        {"sbc" if high == "r1" else "sbci"} r29, {high}\n')
    if opcode == 'sbiw':
        adjustment = f'  108: 6a 97        sbiw r28, {low}\n'
    return ('00000100 <frame>:\n'
            '  100: cf 93        push r28\n'
            '  102: df 93        push r29\n'
            '  104: cd b7        in r28, 0x3d\n'
            '  106: de b7        in r29, 0x3e\n' + adjustment +
            '  10c: de bf        out 0x3e, r29\n'
            '  10e: cd bf        out 0x3d, r28\n'
            '  110: 0e 94 de 0a  call 0x15BC <leaf>\n'
            '  114: 08 95        ret\n'
            '000015BC <leaf>:\n'
            ' 15BC: cf 93        push r28\n'
            ' 15BE: 08 95        ret\n')


class AvrStackTest(unittest.TestCase):
    def test_upper_lower_and_mixed_case_subtraction(self):
        for low in ('0x6A', '0x6a', '0X6A'):
            with self.subTest(low=low):
                result = analyze(frame(low))
                self.assertEqual(result['functions'][0x100][1:3], (2, 106))
                # Saved registers + locals + three-byte Mega call + leaf push.
                self.assertEqual(result['chain'](0x100)[0], 112)

    def test_two_byte_and_sbiw_frames(self):
        for low, high in (('0xAB', '0x01'), ('0xab', '0x01')):
            result = analyze(frame(low, high))
            self.assertEqual(result['functions'][0x100][1:3], (2, 427))
        for low in ('0x2A', '0x2a'):
            result = analyze(frame(low, opcode='sbiw'))
            self.assertEqual(result['functions'][0x100][1:3], (2, 42))

    def test_malformed_operands_are_not_accepted_as_prefixes(self):
        for low, high, opcode in (('0x6G', 'r1', 'subi'),
                                  ('0x6A_bad', 'r1', 'subi'),
                                  ('0x6A', '0x1Z', 'subi'),
                                  ('0x2Aextra', 'r1', 'sbiw')):
            with self.subTest(low=low, high=high, opcode=opcode):
                with self.assertRaisesRegex(ValueError, 'frame immediate'):
                    analyze(frame(low, high, opcode))


if __name__ == '__main__':
    unittest.main()
