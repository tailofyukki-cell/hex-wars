@echo off
rem ============================================================
rem  HEX WARS  ビルド → build と 配布フォルダ を自動更新
rem  このファイルをダブルクリックすればビルドして両方のデータを更新します
rem ============================================================
setlocal

rem --- 場所（このバッチの位置を基準にする） ---
set "PROJ=%~dp0"
set "BUILD=%PROJ%build"
set "DIST=%PROJ%..\HexWars_配布"

rem --- Visual Studio 同梱ツール（PATHに無いのでフルパス指定） ---
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo.
echo === 1/3  コンパイラ環境を準備 ===
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [エラー] Visual Studio が見つかりません: %VS%
    goto :fail
)

if not exist "%BUILD%\CMakeCache.txt" (
    echo === CMake 構成を作成中（初回のみ） ===
    "%CMAKE%" -S "%PROJ%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :fail
)

echo.
echo === 2/3  ビルド ===
"%CMAKE%" --build "%BUILD%"
if errorlevel 1 (
    echo.
    echo [エラー] ビルドに失敗しました。上のメッセージを確認してください。
    goto :fail
)

echo.
echo === 3/3  データを同期（build と 配布フォルダの両方） ===
rem --- build フォルダ（開発時に build\hexwars.exe を直接起動する用） ---
rem  data だけ変えた場合はビルドが走らず build\data が古いままになるので毎回同期する
robocopy "%PROJ%data"   "%BUILD%\data"   /MIR /NFL /NDL /NJH /NJS /NC /NS >nul
robocopy "%PROJ%assets" "%BUILD%\assets" /MIR /NFL /NDL /NJH /NJS /NC /NS >nul

rem --- 配布フォルダ（人に渡す用） ---
if not exist "%DIST%" mkdir "%DIST%"
copy /Y "%BUILD%\hexwars.exe" "%DIST%\hexwars.exe" >nul
for %%D in (SDL2.dll SDL2_ttf.dll SDL2_mixer.dll SDL2_image.dll) do (
    if exist "%BUILD%\%%D" copy /Y "%BUILD%\%%D" "%DIST%\%%D" >nul
)
robocopy "%PROJ%data"   "%DIST%\data"   /MIR /NFL /NDL /NJH /NJS /NC /NS >nul
robocopy "%PROJ%assets" "%DIST%\assets" /MIR /NFL /NDL /NJH /NJS /NC /NS >nul

echo.
echo ============================================================
echo  完了！ 以下の両方を更新しました:
echo    開発用 : %BUILD%\hexwars.exe
echo    配布用 : %DIST%\hexwars.exe
echo ============================================================
echo.
pause
exit /b 0

:fail
echo.
pause
exit /b 1