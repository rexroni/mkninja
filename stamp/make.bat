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
SET stamp=n
FOR /F %%i IN ('dir stamp.c /AA /B 2^>nul') do (SET stamp=o)
:: echo "%make%-%stamp%"

if "%make%%stamp%"=="oo" (echo stamp is up-to-date) else (
    :: /wd4221: ansi compliance
    :: /wd4204: ansi compliance
    cl stamp.c /O2 /W4 /wd4221 /wd4204 /WX /link /out:stamp.exe ^
    && attrib +a stamp.c
)

attrib +a make.bat
