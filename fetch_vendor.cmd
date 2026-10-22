@echo off
:: Fetches the third-party sources that are not kept in git.
:: Boost.Regex (Boost Software License), header-only, used standalone.
setlocal
cd /d "%~dp0"

if exist "vendor\regex\include\boost\regex.hpp" (
    echo Boost.Regex already present.
    exit /b 0
)
git clone --quiet --depth 1 --branch boost-1.89.0 https://github.com/boostorg/regex.git vendor\regex
if errorlevel 1 ( echo Fetching Boost.Regex failed & exit /b 1 )
echo Boost.Regex fetched into vendor\regex.
exit /b 0
