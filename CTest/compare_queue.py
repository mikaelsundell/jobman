# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause

"""Compare queue results and notification traces from two regression executables."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

CASES = ('queue_signals', 'queue_worker_submission', 'queue_dependencies', 'queue_failure_propagation',
         'queue_priority_exclusive', 'queue_batch', 'queue_stop_start', 'queue_restart', 'queue_remove')


def capture(executable, case, child, environment):
    process = subprocess.Popen([str(executable), case, str(child)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace', env=environment,
                               start_new_session=sys.platform != 'win32',
                               creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0)
    try:
        output, _ = process.communicate(timeout=30)
    except BaseException:
        if process.poll() is None:
            if sys.platform == 'win32':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=False,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        raise
    if process.returncode:
        raise RuntimeError('%s %s failed (%s):\n%s' % (executable, case, process.returncode, output))
    reports = [line[7:] for line in output.splitlines() if line.startswith('RESULT ')]
    if len(reports) != 1:
        raise RuntimeError('%s %s did not emit exactly one result:\n%s' % (executable, case, output))
    return json.loads(reports[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--current', required=True, type=Path)
    parser.add_argument('--child', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='JSON comparison report')
    parser.add_argument('--baseline-revision', help='Revision used to build the baseline')
    parser.add_argument('--configuration', choices=('debug', 'release'), default='release')
    parser.add_argument('--repeat', type=int, default=1)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    for path in (args.baseline, args.current, args.child):
        if not path.is_file():
            parser.error('Executable not found: %s' % path)
    environment = dict(os.environ)
    if sys.platform == 'darwin':
        environment['DYLD_IMAGE_SUFFIX'] = '_debug' if args.configuration == 'debug' else ''
    report = {'passed': False, 'baseline_revision': args.baseline_revision, 'comparisons': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        for repetition in range(args.repeat):
            for case in CASES:
                baseline = capture(args.baseline.resolve(), case, args.child.resolve(), environment)
                current = capture(args.current.resolve(), case, args.child.resolve(), environment)
                equal = baseline == current
                report['comparisons'].append(dict(case=case, repetition=repetition + 1, equal=equal,
                                                  baseline=baseline, current=current))
                print('%s %s (%s/%s)' % ('MATCH' if equal else 'DIFFERENT', case, repetition + 1, args.repeat), flush=True)
        report['passed'] = all(row['equal'] for row in report['comparisons'])
    except Exception as error:
        report['error'] = str(error)
        print('FAIL:', error, flush=True)
    finally:
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
