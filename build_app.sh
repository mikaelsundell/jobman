#!/bin/bash
##  Copyright 2022-present Contributors to the jobman project.
##  SPDX-License-Identifier: BSD-3-Clause
##  https://github.com/mikaelsundell/jobman

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
machine_arch=$(uname -m)
macos_version=$(sw_vers -productVersion)
major_version=$(echo "$macos_version" | cut -d '.' -f 1)

# signing
sign_code=OFF
developerid_identity=""
mac_developer_identity=""
mac_installer_identity=""
provisioning_profile=""
provisioning_profile_path=""

sign_app() {
    local bundle_path="$1"
    local sign_type="$2"
    local sign_identity="$3"

    case "$sign_type" in
        dylibs)
            find "$bundle_path" -type f ! -path "*.framework/*" | while read -r file; do
                file_type=$(file "$file")
                if [[ "$file_type" == *"Mach-O 64-bit dynamically linked shared library"* ]] || [[ "$file_type" == *"Mach-O 64-bit bundle"* ]]; then
                    echo "signing dylib $file ..."
                    codesign --force --sign "$sign_identity" --timestamp "$file"
                fi
            done
            ;;
        frameworks)
            find "$bundle_path" -type d -name "*.framework" | while read -r framework; do
                echo "signing framework $framework ..."
                codesign --force --sign "$sign_identity" --timestamp "$framework"
            done
            ;;
        executables)
            find "$bundle_path" -type f | while read -r file; do
                file_type=$(file "$file")
                if [[ "$file_type" == *"Mach-O 64-bit executable"* ]]; then
                    echo "signing executable $file with entitlements ..."
                    echo codesign --force --sign "$sign_identity" --timestamp --options runtime --entitlements "$script_dir/resources/App.entitlements" "$file"
                    codesign --force --sign "$sign_identity" --timestamp --options runtime --entitlements "$script_dir/resources/App.entitlements" "$file"
                fi
            done
            ;;
        *)
            echo "unknown sign type: $sign_type"
            exit 1
            ;;
    esac
}

verify_app() {
    local bundle_path="$1"
    find "$bundle_path" -type f \( -name "*.dylib" -o -name "*.so" -o -name "*.bundle" -o -name "*.framework" -o -perm +111 \) | while read -r file; do
        echo "verifying $file..."
        if codesign --verify --verbose "$file"; then
            echo "signature verification passed for $file"
        else
            echo "signature verification failed for $file"
        fi
    done
}

# check signing
parse_args() {
    while [[ "$#" -gt 0 ]]; do
        case $1 in
            --target=*) 
                major_version="${1#*=}" ;;
            --sign)
                sign_code=ON ;;
            --provisioning=*)
                provisioning_profile="${1#*=}" ;;
            --provisioningpath=*)
                provisioning_profile_path="${1#*=}" ;;
            --github)
                github=ON ;;
            --appstore)
                appstore=ON ;;
            *)
                build_type="$1" # save it in build_type if it's not a recognized flag
                ;;
        esac
        shift
    done
}
parse_args "$@"

# target
if [ -z "$major_version" ]; then
    macos_version=$(sw_vers -productVersion)
    major_version=$(echo "$macos_version" | cut -d '.' -f 1)
fi
export MACOSX_DEPLOYMENT_TARGET=$major_version
export CMAKE_OSX_DEPLOYMENT_TARGET=$major_version

# exit on error
set -e 

# clear
clear

# build type
if [ "$build_type" != "debug" ] && [ "$build_type" != "release" ] && [ "$build_type" != "all" ]; then
    echo "invalid build type: $build_type (use 'debug', 'release', or 'all')"
    exit 1
fi

echo "Building Jobman for $build_type"
echo "---------------------------------"

# signing
if [ "$sign_code" == "ON" ]; then
    default_developerid_identity=${DEVELOPERID_IDENTITY:-}
    default_mac_developer_identity=${MAC_DEVELOPER_IDENTITY:-}
    default_mac_installer_identity=${MAC_INSTALLER_IDENTITY:-}

    read -p "enter Developer ID certificate identity [$default_developerid_identity]: " input_developerid_identity
    developerid_identity=${input_developerid_identity:-$default_developerid_identity}

    if [[ ! "$developerid_identity" == *"Developer ID"* ]]; then
        echo "Developer ID certificate identity must contain 'Developer ID', required for github distribution."
    fi

    read -p "enter Mac Developer certificate Identity [$default_mac_developer_identity]: " input_mac_developer_identity
    mac_developer_identity=${input_mac_developer_identity:-$default_mac_developer_identity}

    if [[ ! "$mac_developer_identity" == *"3rd Party Mac Developer Application"* ]]; then
        echo "Mac Developer installer identity must contain '3rd Party Mac Developer Installer', required for appstore distribution."
    fi

    read -p "enter Mac Installer certificate Identity [$default_mac_installer_identity]: " input_mac_installer_identity
    mac_installer_identity=${input_mac_installer_identity:-$default_mac_installer_identity}

    if [[ ! "$mac_installer_identity" == *"3rd Party Mac Developer Installer"* ]]; then
        echo "Mac Developer installer identity must contain '3rd Party Mac Developer Installer', required for appstore distribution."
    fi
    echo ""
fi

# check if cmake is in the path
if ! command -v cmake &> /dev/null; then
    echo "cmake not found in the PATH, will try to set to /Applications/CMake.app/Contents/bin"
    export PATH=$PATH:/Applications/CMake.app/Contents/bin
    if ! command -v cmake &> /dev/null; then
        echo "cmake could not be found, please make sure it's installed"
        exit 1
    fi
fi

# check if cmake version is compatible
if ! [[ $(cmake --version | grep -o '[0-9]\+\(\.[0-9]\+\)*' | head -n1) < "3.28.0" ]]; then
    echo "cmake version is not compatible with Qt, must be before 3.28.0 for multi configuration"
    exit 1;
fi

# build automator
build_automator() {
    local build_type="$1"

    # cmake
    export PATH=$PATH:/Applications/CMake.app/Contents/bin &&

    # script dir
    cd "$script_dir"

    # clean dir
    build_dir="$script_dir/build.$build_type"
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi

    # build dir
    mkdir -p "build.$build_type"
    cd "build.$build_type"

    # prefix dir
    if ! [ -d "$THIRDPARTY_DIR" ]; then
        echo "could not find 3rdparty project in: $THIRDPARTY_DIR"
        exit 1
    fi
    prefix="$THIRDPARTY_DIR"

    # xcode build
    xcode_type=$(echo "$build_type" | awk '{ print toupper(substr($0, 1, 1)) tolower(substr($0, 2)) }')

    # build
    if [ -n "$provisioning_profile" ] && [ -n "$provisioning_profile_path" ]; then
        cmake .. -DCMAKE_MODULE_PATH="$script_dir/modules" -DCMAKE_PREFIX_PATH="$prefix" -DPROVISIONING_PROFILE="$provisioning_profile" -G Xcode
    else
        cmake .. -DCMAKE_MODULE_PATH="$script_dir/modules" -DCMAKE_PREFIX_PATH="$prefix" -G Xcode
    fi
    cmake --build . --config $xcode_type --parallel &&

    if [ "$github" == "ON" ]; then
        dmg_file="$script_dir/Automator_macOS${major_version}_${machine_arch}_${build_type}.dmg"
        if [ -f "$dmg_file" ]; then
            rm -f "$dmg_file"
        fi

        # deploy
        $script_dir/scripts/macdeploy.sh -b "$xcode_type/Automator.app" -m "$prefix/bin/macdeployqt"

        if [ -n "$developerid_identity" ]; then
            if [ "$sign_code" == "ON" ]; then
                codesign --force --deep --sign "$developerid_identity" --timestamp --options runtime "$xcode_type/Automator.app"
            fi
        else 
            echo "Developer ID identity must be set for github distribution, sign will be skipped."
        fi

        # deploydmg
        $script_dir/scripts/macdmg.sh -b "$xcode_type/Automator.app" -d "$dmg_file"
        if [ -n "$developerid_identity" ]; then
            if [ "$sign_code" == "ON" ]; then
                codesign --force --deep --sign "$developerid_identity" --timestamp --options runtime --verbose "$dmg_file"
            fi
        else 
            echo "Developer ID identity must be set for github distribution, sign will be skipped."
        fi
    fi

    if [ "$appstore" == "ON" ]; then
        pkg_file="$script_dir/Automator_macOS${major_version}_${machine_arch}_${build_type}.pkg"
        if [ -f "$pkg_file" ]; then
            rm -f "$pkg_file"
        fi

        # provisioning
        if [ -n "$provisioning_profile" ] && [ -n "$provisioning_profile_path" ]; then
            cp -f "$provisioning_profile_path" "$xcode_type/Automator.app/Contents/embedded.provisionprofile"
        else
            echo "Provisioning profile and path must be set for appstore distribution, will be skipped."
        fi

        # deploy
        $script_dir/scripts/macdeploy.sh -b "$xcode_type/Automator.app" -m "$prefix/bin/macdeployqt"
        if [ -n "$mac_developer_identity" ]; then
            if [ "$sign_code" == "ON" ]; then
                # entitlements
                teamid=$(echo "$mac_developer_identity" | awk -F '[()]' '{print $2}')
                applicationid=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "$xcode_type/Automator.app/Contents/Info.plist")
                entitlements="$script_dir/resources/App.entitlements"
                echo sed -e "s/\${TEAMID}/$teamid/g" -e "s/\${APPLICATIONIDENTIFIER}/$applicationid/g" "$script_dir/resources/App.entitlements.in" > "$entitlements"
                sed -e "s/\${TEAMID}/$teamid/g" -e "s/\${APPLICATIONIDENTIFIER}/$applicationid/g" "$script_dir/resources/App.entitlements.in" > "$entitlements"
                # sign
                sign_app "$xcode_type/Automator.app" "dylibs" "$mac_developer_identity"
                sign_app "$xcode_type/Automator.app" "frameworks" "$mac_developer_identity"
                sign_app "$xcode_type/Automator.app" "executables" "$mac_developer_identity"
                verify_app "$xcode_type/Automator.app"
                codesign --force --deep --sign "$mac_developer_identity" --entitlements $entitlements "$xcode_type/Automator.app"
                codesign --verify "$xcode_type/Automator.app"
            fi
        else 
            echo "Mac Developer identity must be set for appstore distribution, sign will be skipped."
        fi

        # productbuild
        if [ "$sign_code" == "ON" ]; then
            if [ -n "$mac_installer_identity" ]; then
                productbuild --component "$xcode_type/Automator.app" "/Applications" --sign "${mac_installer_identity}" --product "$xcode_type/Automator.app/Contents/Info.plist" "$pkg_file" 
            else 
                echo "Mac Installer identity must be set for appstore distribution, sign will be skipped."
                productbuild --component "$xcode_type/Automator.app" "/Applications" --product "$xcode_type/Automator.app/Contents/Info.plist" "$pkg_file" 
            fi  
        else
            productbuild --component "$xcode_type/Automator.app" "/Applications" --product "$xcode_type/Automator.app/Contents/Info.plist" "$pkg_file" 
        fi
    fi
}

# build types
if [ "$build_type" == "all" ]; then
    build_automator "debug"
    build_automator "release"
else
    build_automator "$build_type"
fi