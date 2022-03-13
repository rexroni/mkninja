@echo off

:: Note: this is how you can set a variable to the output of a command:
::   FOR /F %%i IN ('MY COMMAND') do (SET myvar=%%i)
:: We are passing /AA to dir so it only lists archived files.
:: The archive bit of a file is cleared whenever it is modified, so if we
:: archive right after compiling a file, we will know if we need to recompile
:: later.

:: detect which files are still archived
SET make=n
FOR /F %%i IN ('dir make.bat /AA /B 2^>nul') do (SET make=o)
SET winpwd=n
FOR /F %%i IN ('dir winpwd.c /AA /B 2^>nul') do (SET winpwd=o)
SET findglob=n
FOR /F %%i IN ('dir findglob.c /AA /B 2^>nul') do (SET findglob=o)
SET main=n
FOR /F %%i IN ('dir main.c /AA /B 2^>nul') do (SET main=o)
SET test=n
FOR /F %%i IN ('dir test.c /AA /B 2^>nul') do (SET test=o)
:: echo "%make%-%winpwd%-%findglob%-%main%-%test%"

if "%make%%winpwd%"=="oo" (echo winpwd is up-to-date) else (
    :: winpwd.c is not archived; rebuild and rearchive
    cl winpwd.c && attrib +a winpwd.c
)

if "%make%%findglob%%main%"=="ooo" (echo findglob is up-to-date) else (
    :: /wd4221: ansi compliance
    :: /wd4204: ansi compliance
    cl main.c /O2 /W4 /wd4221 /wd4204 /WX /link /out:findglob.exe ^
    && attrib +a findglob.c && attrib +a main.c
)

if "%make%%findglob%%test%"=="ooo" (echo test is up-to-date) else (
    :: /wd4221: ansi compliance
    :: /wd4204: ansi compliance
    FOR /F %%i IN ('winpwd %cd%') do (SET PWD=%%i)
    echo "PWD=%PWD%"
    cl test.c /O2 /W4 /wd4221 /wd4204 /WX /D_CRT_SECURE_NO_WARNINGS ^
        /D_CRT_NONSTDC_NO_WARNINGS /DCWD=\"%PWD%/\" ^
    && attrib +a findglob.c && attrib +a test.c
)

attrib +a make.bat
