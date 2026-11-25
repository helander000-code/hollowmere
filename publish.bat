@echo off
setlocal
cd /d "%~dp0"
echo === Hollowmere publish ===
where git >nul 2>nul || winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
where gh >nul 2>nul || winget install --id GitHub.cli -e --accept-source-agreements --accept-package-agreements
set "PATH=%PATH%;C:\Program Files\Git\cmd;C:\Program Files\GitHub CLI"
git --version || (echo Git not found. Close this window and run again. & pause & exit /b 1)
gh --version >nul || (echo GitHub CLI not found. Close this window and run again. & pause & exit /b 1)
git config --global user.name "helander000-code"
git config --global user.email "helander000-code@users.noreply.github.com"
gh auth status >nul 2>nul || gh auth login -h github.com -p https -w
gh auth setup-git
if not exist media mkdir media
copy /y shots\promo1.png media\ >nul
copy /y shots\promo2.png media\ >nul
if not exist .git git init -b main
git add .
git commit -m "Hollowmere: initial release" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>" -m "Claude-Session: https://claude.ai/code/session_01X9b8LQ7Z4kq9SMHCh8yuyE"
gh repo create hollowmere --public --source . --push
gh release create v1.0 release\Hollowmere.zip --title "Hollowmere v1.0" --notes "Windows 10/11, DirectX 12. Unzip, run Hollowmere.exe, keep the textures folder beside it. SmartScreen: More info, Run anyway."
echo.
echo Release page: https://github.com/helander000-code/hollowmere/releases/tag/v1.0
echo Direct download: https://github.com/helander000-code/hollowmere/releases/latest/download/Hollowmere.zip
pause
