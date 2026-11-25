# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause

"""Build and run Jobman's registered CTest regressions, retaining logs and reports."""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]


def cache_value(directory, key):
    cache = directory / 'CMakeCache.txt'
    if cache.is_file():
        for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
            if line.startswith(key + ':') and '=' in line:
                return line.split('=', 1)[1]
    return None


def tools(directory, override):
    cached = cache_value(directory, 'CMAKE_COMMAND')
    cmake = override or (cached if cached and Path(cached).is_file() else shutil.which('cmake'))
    cmake = shutil.which(cmake) if cmake else None
    if not cmake:
        raise RuntimeError('CMake is required. Put it on PATH or pass --cmake PATH.')
    sibling = Path(cmake).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
    ctest = str(sibling) if sibling.is_file() else shutil.which('ctest')
    if not ctest:
        raise RuntimeError('CTest is required alongside CMake.')
    return cmake, ctest


def run_logged(command, log, environment):
    command = [str(argument) for argument in command]
    print('\nRUN', subprocess.list2cmdline(command), '\nLOG', log, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        process = subprocess.Popen(
            command, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding='utf-8', errors='replace',
            start_new_session=sys.platform != 'win32',
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0)
        try:
            for line in process.stdout:
                stream.write(line)
                stream.flush()
                print(line, end='', flush=True)
            return process.wait()
        except BaseException:
            if process.poll() is None:
                if sys.platform == 'win32':
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=False,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise


def run_configuration(args, configuration, output):
    directory = Path(args.build_dir).resolve() if args.build_dir else ROOT / ('build.' + configuration.lower())
    row = {'configuration': configuration, 'build_dir': str(directory), 'passed': False}
    output.mkdir()
    environment = dict(os.environ)
    if sys.platform == 'darwin':
        environment['DYLD_IMAGE_SUFFIX'] = '_debug' if configuration == 'Debug' else ''
    try:
        if not (directory / 'CMakeCache.txt').is_file():
            raise RuntimeError('No configured build at %s. Set THIRDPARTY_DIR to your matching Qt dependency '
                               'prefix and run build_app for %s first.' % (directory, configuration))
        if not cache_value(directory, 'CMAKE_CONFIGURATION_TYPES'):
            if cache_value(directory, 'CMAKE_BUILD_TYPE') != configuration:
                raise RuntimeError('Build configuration does not match %s. Use a matching build directory.' % configuration)
        cmake, ctest = tools(directory, args.cmake)
        if not args.no_build:
            code = run_logged([cmake, '-S', ROOT, '-B', directory, '-DBUILD_TESTING=ON'],
                              output / 'configure.log', environment)
            if code:
                raise RuntimeError('Configure failed; tests were not run against stale binaries.')
            code = run_logged([cmake, '--build', directory, '--config', configuration, '--target',
                               'Jobman', 'jobman_tests', '--parallel', args.jobs], output / 'build.log', environment)
            if code:
                raise RuntimeError('Build failed; tests were not run against stale binaries.')
        elif cache_value(directory, 'BUILD_TESTING') != 'ON':
            raise RuntimeError('BUILD_TESTING is not enabled. Run test_app without --no-build first.')
        command = [ctest, '--test-dir', directory, '-C', configuration, '--output-on-failure', '--no-tests=error']
        if args.filter:
            command.extend(['-R', args.filter])
        command.extend(['--output-junit', output / 'ctest.xml'])
        row['passed'] = run_logged(command, output / 'ctest.log', environment) == 0
    except Exception as error:
        row['error'] = str(error)
        print('FAIL', configuration, error, flush=True)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', nargs='?', default='debug', type=str.lower,
                        choices=('debug', 'release', 'all'))
    parser.add_argument('--no-build', action='store_true', help='Use existing test binaries without rebuilding')
    parser.add_argument('--build-dir', help='Override the configured build directory; single configuration only')
    parser.add_argument('--output', help='New directory for logs, summary.json and JUnit results')
    parser.add_argument('--cmake', help='CMake executable override')
    parser.add_argument('--filter', help='CTest test-name regular expression')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 6), help='Parallel build jobs')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.configuration == 'all' and args.build_dir:
        parser.error('--build-dir requires a single configuration')
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
    output = Path(args.output).resolve() if args.output else ROOT / 'build.test-results' / stamp
    if output.exists():
        parser.error('--output must be a new directory: %s' % output)
    output.mkdir(parents=True)
    summary = {'passed': False, 'status': 'running', 'configurations': []}
    summary_file = output / 'summary.json'

    def save():
        summary_file.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')

    save()
    print('Jobman tests. Results:', output, flush=True)
    configurations = ('Debug', 'Release') if args.configuration == 'all' else (args.configuration.capitalize(),)
    try:
        for configuration in configurations:
            summary['configurations'].append(run_configuration(args, configuration, output / configuration.lower()))
            save()
    except KeyboardInterrupt:
        summary['status'] = 'interrupted'
        save()
        return 130
    summary['passed'] = all(row['passed'] for row in summary['configurations'])
    summary['status'] = 'passed' if summary['passed'] else 'failed'
    save()
    print('\n%s: %s' % ('PASS ALL' if summary['passed'] else 'FAIL', summary_file), flush=True)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
