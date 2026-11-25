@echo off
REM Copyright 2022-present Contributors to the jobman project.
REM SPDX-License-Identifier: BSD-3-Clause
setlocal DisableDelayedExpansion
where py >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0CTest\run_all.py" %*
exit /b %errorlevel%
:python
where python >nul 2>&1
if errorlevel 1 goto missing
python "%~dp0CTest\run_all.py" %*
exit /b %errorlevel%
:missing
echo Python 3 is required to run Jobman tests. Install Python 3 and add it to PATH. 1>&2
exit /b 1
