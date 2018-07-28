::::::::::::::::::::::::::::::::::::::::
:: SpoutRenderer Demo (360p)
::::::::::::::::::::::::::::::::::::::::
@echo off
cd "%~dp0"

:: start SpoutReceiver
start bin\SpoutReceiver

:: CLSID constants
set CLSID_LAVSplitterSource={B98D13E7-55DB-4385-A33D-09FD1BA26338}
set CLSID_LAVVideoDecoder={EE30215D-164F-4A92-A4EB-9D4C13390F9F}
set CLSID_SpoutRenderer={19B0E3A6-E681-4AF6-A10E-BE02C5D25E3F}

:: make sure that also LAV's DLLs are found
set PATH=filters;%PATH%

:: render 360p MP4 video with SpoutRenderer
bin\dscmd^
 -graph ^
%CLSID_LAVSplitterSource%;src=..\assets\bbb_360p_10sec.mp4;file=LAVSplitter.ax,^
%CLSID_LAVVideoDecoder%;file=LAVVideo.ax,^
%CLSID_SpoutRenderer%;file=SpoutRenderer.ax^
!0:1,1:2^
 -noWindow^
 -loop -1^
 -i
 
echo.
pause
