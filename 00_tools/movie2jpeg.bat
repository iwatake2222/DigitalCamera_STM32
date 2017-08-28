cd /d %~dp1
mkdir "%~n1"
setlocal
set width=320
set fps=10
set ffmpeg="C:\asd\tool\ffmpeg-20170827-ef0c6d9-win64-static\bin\ffmpeg.exe"
%ffmpeg% -i %1 -r %fps% -vf scale=%width%:%width%/dar -q 5 -f image2 "%~n1\f_%%05d.jpg"

cd "%~n1"
copy /b f_*.jpg "%~n1.avi"

endlocal
