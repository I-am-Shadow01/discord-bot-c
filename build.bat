@echo off
setlocal

echo Checking for compiler...

where cl >nul 2>nul
if %errorlevel%==0 (
    echo Found MSVC. Compiling...
    cl /nologo /D_CRT_SECURE_NO_WARNINGS main.c discord.c config.c gemini.c cJSON.c /link winhttp.lib user32.lib /out:bot.exe
    if %errorlevel%==0 (
        echo Build successful! Run with: bot.exe
    ) else (
        echo Build failed.
    )
    goto :EOF
)

where gcc >nul 2>nul
if %errorlevel%==0 (
    echo Found GCC. Compiling...
    gcc -o bot.exe main.c discord.c config.c gemini.c cJSON.c -lwinhttp
    if %errorlevel%==0 (
        echo Build successful! Run with: bot.exe
    ) else (
        echo Build failed.
    )
    goto :EOF
)

echo No suitable compiler found. Checked for cl and gcc.
echo Please install Visual Studio Community - Desktop C++ or MinGW-w64.
exit /b 1
