# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause

"""Guard runner failure handling and configuration-specific Qt runtime selection."""

import argparse
from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_all


class RunnerTests(unittest.TestCase):
    def invoke(self, directory, configuration='Release', codes=(0, 0, 0), no_build=False):
        args = argparse.Namespace(build_dir=str(directory), cmake=None, no_build=no_build,
                                  jobs=2, filter=None)
        with patch.object(run_all, 'tools', return_value=('cmake', 'ctest')), \
                patch.object(run_all, 'run_logged', side_effect=codes) as logged, \
                redirect_stdout(io.StringIO()):
            result = run_all.run_configuration(args, configuration, directory / 'results')
        return result, logged

    def cache(self, directory, configuration='Release', testing='ON'):
        (directory / 'CMakeCache.txt').write_text(
            'CMAKE_BUILD_TYPE:STRING=%s\nBUILD_TESTING:BOOL=%s\n' % (configuration, testing), encoding='utf-8')

    def test_missing_build_is_actionable(self):
        with tempfile.TemporaryDirectory() as name:
            result, logged = self.invoke(Path(name))
            self.assertFalse(result['passed'])
            self.assertIn('THIRDPARTY_DIR', result['error'])
            logged.assert_not_called()

    def test_failed_configure_and_build_do_not_run_stale_tests(self):
        for codes, expected_calls in (((1,), 1), ((0, 1), 2)):
            with self.subTest(codes=codes), tempfile.TemporaryDirectory() as name:
                directory = Path(name)
                self.cache(directory)
                result, logged = self.invoke(directory, codes=codes)
                self.assertFalse(result['passed'])
                self.assertEqual(logged.call_count, expected_calls)
                self.assertNotIn('ctest', [call.args[0][0] for call in logged.call_args_list])

    def test_ctest_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.cache(directory)
            result, logged = self.invoke(directory, codes=(0, 0, 8))
            self.assertFalse(result['passed'])
            self.assertIn('--no-tests=error', logged.call_args.args[0])
            self.assertIn('--output-junit', logged.call_args.args[0])

    def test_wrong_single_configuration_is_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.cache(directory, configuration='Debug')
            result, logged = self.invoke(directory)
            self.assertFalse(result['passed'])
            logged.assert_not_called()

    def test_no_build_requires_enabled_tests(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.cache(directory, testing='OFF')
            result, logged = self.invoke(directory, no_build=True)
            self.assertFalse(result['passed'])
            logged.assert_not_called()

    def test_mac_runtime_suffix_matches_configuration(self):
        for configuration, suffix in (('Debug', '_debug'), ('Release', '')):
            with self.subTest(configuration=configuration), tempfile.TemporaryDirectory() as name:
                directory = Path(name)
                self.cache(directory, configuration=configuration)
                with patch.object(run_all.sys, 'platform', 'darwin'), \
                        patch.dict(os.environ, {'DYLD_IMAGE_SUFFIX': '_debug'}):
                    result, logged = self.invoke(directory, configuration=configuration)
                self.assertTrue(result['passed'])
                for call in logged.call_args_list:
                    self.assertEqual(call.args[2]['DYLD_IMAGE_SUFFIX'], suffix)


if __name__ == '__main__':
    unittest.main()
