@echo off
setlocal
set "SIGNTOOL=F:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set "INF2CAT=F:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe"
set "SHA1=34AFF96C57E9ADE68B23B4828859CF9B7F4EF442"
cd /d "%~dp0\..\..\output"
echo --- Signing atikmdag.sys ---
"%SIGNTOOL%" sign /sha1 %SHA1% /fd SHA256 /t http://timestamp.digicert.com atikmdag.sys
echo SYS_SIGN_EXIT=%errorlevel%
echo --- Regenerating CAT ---
"%INF2CAT%" /driver:. /os:10_X64
echo INF2CAT_EXIT=%errorlevel%
echo --- Signing CAT ---
"%SIGNTOOL%" sign /sha1 %SHA1% /fd SHA256 /t http://timestamp.digicert.com amdbc250_dream.cat
echo CAT_SIGN_EXIT=%errorlevel%
echo --- Verify ---
"%SIGNTOOL%" verify /pa /v atikmdag.sys | findstr /i "signing certificate verified"
endlocal
