@echo off
REM Copyright 2022-present Contributors to the jobman project.
REM SPDX-License-Identifier: BSD-3-Clause
REM https://github.com/mikaelsundell/jobman

setlocal EnableExtensions DisableDelayedExpansion
set "app_dir=%~dp0"
set "app_name=Jobman"
set "pkg_name=jobman"
set "build_type="
set "deploy=0"
set "cmake_generator="
set "cmake_exe=cmake.exe"
set "cmake_instance="

:parse_args
if "%~1"=="" goto done_parse_args
set "arg=%~1"
if /I "%arg:~0,18%"=="--cmake_generator=" (
    set "cmake_generator=%arg:~18%"
) else if /I "%arg%"=="--deploy" (
    set "deploy=1"
) else (
    set "build_type=%arg%"
)
shift
goto parse_args
:done_parse_args

if /I "%build_type%"=="debug" goto valid_build_type
if /I "%build_type%"=="release" goto valid_build_type
if /I "%build_type%"=="all" goto valid_build_type
echo Usage: build_app.bat Debug^|Release^|All [--deploy] [--cmake_generator=NAME] 1>&2
exit /b 1
:valid_build_type

REM Validate before creating build directories or trying to locate compilers.
if not defined THIRDPARTY_DIR (
    echo THIRDPARTY_DIR is not set. 1>&2
    echo Set it to the dependency install prefix matching Debug or Release: 1>&2
    echo   set "THIRDPARTY_DIR=C:\path\to\dependency-install-prefix" 1>&2
    echo The prefix must contain lib\cmake\Qt6\Qt6Config.cmake. 1>&2
    exit /b 1
)
if not exist "%THIRDPARTY_DIR%\." (
    echo THIRDPARTY_DIR does not point to a valid directory: "%THIRDPARTY_DIR%" 1>&2
    exit /b 1
)
if not exist "%THIRDPARTY_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo Qt6Config.cmake was not found under THIRDPARTY_DIR: "%THIRDPARTY_DIR%" 1>&2
    echo Select the dependency install prefix, rather than the 3rdparty source directory. 1>&2
    exit /b 1
)

if defined cmake_generator (
    where cmake.exe >nul 2>&1
    if errorlevel 1 (
        echo CMake 3.27 or newer is required. Install it and add its bin directory to PATH. 1>&2
        exit /b 1
    )
) else (
    call :find_visual_studio
    if errorlevel 1 exit /b 1
)
"%cmake_exe%" --version
if errorlevel 1 exit /b 1

if /I "%build_type%"=="all" (
    call :build_jobman Debug
    if errorlevel 1 exit /b 1
    call :build_jobman Release
    if errorlevel 1 exit /b 1
    exit /b 0
)
if /I "%build_type%"=="debug" (
    call :build_jobman Debug
) else (
    call :build_jobman Release
)
exit /b %errorlevel%

:find_visual_studio
REM Match Stageviz's installed Visual Studio / Build Tools discovery.
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" goto missing_visual_studio
set "vs_install="
set "vs_version="
for /f "usebackq tokens=1,*" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format text`) do (
    if "%%I"=="installationPath:" set "vs_install=%%J"
    if "%%I"=="installationVersion:" set "vs_version=%%J"
)
if not defined vs_install goto missing_visual_studio
if not defined vs_version goto missing_visual_studio
for /f "delims=." %%V in ("%vs_version%") do set "vs_major=%%V"
where cmake.exe >nul 2>&1
if not errorlevel 1 call :find_cmake_generator
if defined cmake_generator goto found_cmake
set "cmake_exe=%vs_install%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%cmake_exe%" call :find_cmake_generator
if not defined cmake_generator (
    echo No CMake supports the installed Visual Studio %vs_version%. 1>&2
    echo Update CMake on PATH or install Visual Studio's C++ CMake tools. 1>&2
    exit /b 1
)
:found_cmake
set cmake_instance="-DCMAKE_GENERATOR_INSTANCE=%vs_install%"
exit /b 0

:find_cmake_generator
for /f "tokens=1-4 delims=* " %%G in ('"%cmake_exe%" --help') do (
    if "%%G %%H %%I"=="Visual Studio %vs_major%" set "cmake_generator=%%G %%H %%I %%J"
)
exit /b 0

:missing_visual_studio
echo Visual Studio or Build Tools with the MSVC C++ tools and a Windows SDK is required. 1>&2
echo Install the Desktop development with C++ workload, or pass --cmake_generator for another generator. 1>&2
exit /b 1

:build_jobman
set "build_type=%~1"
set "build_dir=%app_dir%build.%build_type%"
echo Building Jobman for %build_type% using %cmake_generator%
echo Dependency prefix: "%THIRDPARTY_DIR%"
echo Build directory: "%build_dir%"
"%cmake_exe%" -S "%app_dir%." -B "%build_dir%" -G "%cmake_generator%" %cmake_instance% -DCMAKE_MODULE_PATH="%app_dir%modules" -DCMAKE_PREFIX_PATH="%THIRDPARTY_DIR%" -DCMAKE_BUILD_TYPE=%build_type%
if errorlevel 1 exit /b 1
"%cmake_exe%" --build "%build_dir%" --config %build_type% --parallel --verbose
if errorlevel 1 exit /b 1
if "%deploy%"=="1" (
    call :deploy_jobman
    if errorlevel 1 exit /b 1
)
exit /b 0

:deploy_jobman
setlocal EnableDelayedExpansion
REM deploy using windeployqt
set "deploy_dir=%app_dir%deploy.%build_type%"
set "windeployqt=%THIRDPARTY_DIR%\bin\windeployqt6.exe"
set "exe_path=%build_dir%\%build_type%\%app_name%.exe"
set "presets_path=%build_dir%\%build_type%\Presets"
set "resources_path=%build_dir%\%build_type%\Resources"

echo Deploying Jobman for %build_type% to %deploy_dir%
echo -------------------------------------------------

REM check if windeployqt exists
if not exist "%windeployqt%" (
    echo windeployqt not found in %THIRDPARTY_DIR%/bin
    endlocal & exit /b 1
)

REM clean deploy directory
if exist "%deploy_dir%" rmdir /s /q "%deploy_dir%"
mkdir "%deploy_dir%"

REM copy executable
copy "%exe_path%" "%deploy_dir%"

REM copy resources
xcopy "%presets_path%" "%deploy_dir%\Presets" /E /I /Y
xcopy "%resources_path%" "%deploy_dir%\Resources" /E /I /Y

REM copy dependencies
copy "%THIRDPARTY_DIR%\bin\zlib.dll" "%deploy_dir%"
copy "%THIRDPARTY_DIR%\bin\libpng16.dll" "%deploy_dir%"

REM run windeployqt
"%windeployqt%" "%deploy_dir%\%app_name%.exe" --dir "%deploy_dir%"
if errorlevel 1 (
    endlocal
    exit /b 1
)

echo deployment successful

REM create a zip file of the deployment folder

REM extract version from CMakeLists.txt
set "version_file=%app_dir%CMakeLists.txt"
set "version="

for /f "usebackq tokens=3 delims=() " %%A in (`findstr /c:"set (app_long_version" "%version_file%"`) do (
    set "version=%%A"
    set "version=!version:~1,-1!"
)

if "%version%"=="" (
    echo failed to extract version from CMakeLists.txt
    endlocal & exit /b 1
)

for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value ^| find "LocalDateTime"') do set datetime=%%I
set "current_date=%datetime:~2,6%"

set "zipfile=%deploy_dir%\%app_name%_%version%_%current_date%_%build_type%.zip"
echo creating zip file: %zipfile%
powershell -command "Compress-Archive -Path '%deploy_dir%\*' -DestinationPath '%zipfile%' -Force"

if errorlevel 1 (
    echo failed to create ZIP file
    endlocal & exit /b 1
)

echo zip file created successfully: %zipfile%

endlocal & exit /b 0
