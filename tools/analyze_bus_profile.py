#!/usr/bin/env python3
"""Summarize BUS v1 capture logs; no external packages. Preserve original logs too.

Counters show frames serviced by this controller, not true wire utilization.
Timestamps on events are processing times, not CAN hardware arrival timestamps.
"""
import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re

SUMMARY_FIELDS = ('ms rx rx_drop invalid hw_overflow rx_high_water service_gap_us queue_age_ms '
                  'neg_isr tx_info tx_data tx_describe tx_setting tx_ok tx_fail tx_in_neg '
                  'neg_rx info_rx data_rx broadcast_rx ack_rx other_rx event_drop eflg tec rec').split()
UNIT_FIELDS = ('ms slot address identity issue phase fan_requested fan_ack fan_reason status '
               'status_age_ms response_age_ms broadcast_age_ms input_c output_c output_v output_a '
               'input_v authorized_ca limited_ca ack_raw authorized_cv flags').split()
FANS = {0: 'UNKNOWN', 1: 'AUTO', 2: 'MAX'}
COUNTERS_32 = {'rx', 'rx_drop', 'invalid'}
COUNTERS_16 = set('neg_isr tx_info tx_data tx_describe tx_setting tx_ok tx_fail tx_in_neg '
                  'neg_rx info_rx data_rx broadcast_rx ack_rx other_rx'.split())

@dataclass
class Capture:
    summaries: list = field(default_factory=list)
    units: list = field(default_factory=list)
    events: list = field(default_factory=list)
    malformed: int = 0

def parse(text):
    captures = [Capture()]
    for raw in text.replace('\r', '\n').splitlines():
        match = re.search(r'BUS ([SUE])([, ])(.*)', raw)
        if not match:
            continue
        kind, _, payload = match.groups()
        current = captures[-1]
        try:
            if kind == 'E':
                if not re.match(r't=\d+ \S+ (?:addr|slot)=\d+ ', payload):
                    raise ValueError('event')
                current.events.append(payload)
                continue
            fields = SUMMARY_FIELDS if kind == 'S' else UNIT_FIELDS
            parts = payload.strip().split(',')
            if len(parts) != len(fields):
                raise ValueError('field count')
            item = dict(zip(fields, parts))
            for key in fields:
                if key == 'identity':
                    if not re.fullmatch('[0-9A-Fa-f]{12}', item[key]):
                        raise ValueError('identity')
                elif item[key] == '-' and kind == 'U':
                    item[key] = None
                elif key in ('input_c', 'output_c', 'output_v', 'output_a', 'input_v'):
                    item[key] = float(item[key])
                else:
                    item[key] = int(item[key])
            if item['ms'] is None or not 0 <= item['ms'] <= 0xffffffff:
                raise ValueError('timestamp')
            if kind == 'S':
                if any(v < 0 or v > 0xffffffff for v in item.values()):
                    raise ValueError('counter')
                # Summary output is chronological; queued event timestamps need
                # not be. A large backwards jump is millis rollover, not reboot.
                if current.summaries and 0 < current.summaries[-1]['ms'] - item['ms'] < 0x80000000:
                    current = Capture()
                    captures.append(current)
                current.summaries.append(item)
            else:
                current.units.append(item)
        except (ValueError, TypeError):
            current.malformed += 1
    return captures

def delta(rows, field):
    """Observed increments between summaries; earlier/missing capture is unknown."""
    bits = 32 if field in COUNTERS_32 else 16
    return sum((b[field]-a[field]) % (1 << bits) for a, b in zip(rows, rows[1:]))

def describe_event(event):
    m = re.match(r't=(\d+) (\S+).*value=(\d+) detail=(\d+)', event)
    if not m:
        return event
    _, kind, value, detail = m.groups()
    value, detail = int(value), int(detail)
    if kind in ('tx-setting', 'rx-ack'):
        reg, high = detail & 255, detail >> 8
        if reg == 0x34:
            event += ' [fan=' + ('MAX' if value else 'AUTO') + ']'
        if kind == 'rx-ack':
            event += ' [accepted]' if high == 1 else ' [REJECTED/unknown reply]'
    elif kind == 'fan-demand':
        temp, amps = value >> 16, value & 65535
        if temp >= 32768:
            temp -= 65536
        event += f' [output C={temp/100 if temp != -32768 else "unknown"}, A={amps/100 if amps != 65535 else "unknown"}]'
    return event

def report(capture, number, events=False):
    print(f'Capture {number}: {len(capture.summaries)} summaries, {len(capture.units)} unit snapshots, '
          f'{len(capture.events)} events; malformed/truncated BUS rows={capture.malformed}')
    rows = capture.summaries
    if rows:
        last = rows[-1]
        elapsed = sum((b['ms']-a['ms']) % (1 << 32) for a, b in zip(rows, rows[1:]))
        print(f'  Summary interval: {elapsed/1000:.1f}s; received {delta(rows,"rx")} frames in interval')
        print(f'  RX drops: last={last["rx_drop"]}, interval=+{delta(rows,"rx_drop")}; '
              f'hardware-overflow events={last["hw_overflow"]}; event-log loss={last["event_drop"]}')
        print(f'  Max service gap={last["service_gap_us"]}us; max queue age={last["queue_age_ms"]}ms; '
              f'queue high-water={last["rx_high_water"]}/15')
        print(f'  MCP2515 latest EFLG=0x{last["eflg"]:02X}, TEC={last["tec"]}, REC={last["rec"]}')
        print(f'  Negotiation frames: ISR={last["neg_isr"]}, processed={last["neg_rx"]}; '
              f'TX attempts during known negotiation={last["tx_in_neg"]} (16-bit counters)')
        print(f'  TX cumulative modulo 65536: INFO={last["tx_info"]}, DATA={last["tx_data"]}, '
              f'DESCRIBE={last["tx_describe"]}, SET={last["tx_setting"]}, '
              f'sent={last["tx_ok"]}, failed={last["tx_fail"]}')
        if last['rx_drop'] or last['hw_overflow'] or last['event_drop'] or capture.malformed:
            print('  Capture has losses: absence of an event is not proof it did not happen.')
    latest = {}
    for row in capture.units:
        latest[row['slot']] = row
    for slot, row in sorted(latest.items()):
        status = row['status']
        reported = 'unknown' if status is None else ('MAX' if status & (1 << 10) else 'AUTO')
        print(f'  PSU {slot} {row["identity"]} addr={row["address"]}: '
              f'fan requested={FANS.get(row["fan_requested"],"?")}, ACK={FANS.get(row["fan_ack"],"?")}, '
              f'status-bit10={reported} (age={row["status_age_ms"]}ms); '
              f'Tin/out={row["input_c"]}/{row["output_c"]} C, V/A={row["output_v"]}/{row["output_a"]}')
    if events:
        for event in capture.events:
            print('  ' + describe_event(event))
    print('  Fan ACK/status bits are not RPM measurements. Counter deltas can undercount multiple wraps in a long capture gap.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--events', action='store_true', help='also print decoded event timeline')
    args = parser.parse_args()
    found = False
    for path in args.logs:
        print(str(path))
        for number, capture in enumerate(parse(path.read_text(errors='replace')), 1):
            if capture.summaries or capture.events or capture.units or capture.malformed:
                found = True
                report(capture, number, args.events)
    if not found:
        parser.exit(1, 'No BUS v1 records found; use a profiling/observer firmware build.\n')

if __name__ == '__main__':
    main()
