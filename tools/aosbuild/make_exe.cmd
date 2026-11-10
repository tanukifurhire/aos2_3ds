@echo off
rem Builds aosbuild.exe (the GUI builder) into the repo root. Needs Go on PATH: https://go.dev/dl/
cd /d "%~dp0"
go build -ldflags "-H=windowsgui -s -w" -o ..\..\aosbuild.exe .
if errorlevel 1 (
	echo.
	echo Build failed. Is Go installed and on PATH?
	pause
	exit /b 1
)
echo Built aosbuild.exe in the repo root. Double-click it to open the builder.
"%SystemRoot%\System32\timeout.exe" /t 3 >nul
