::::::::::::::::::::::::::::::::::::::::
:: SpoutGrabber Demo (360p)
::::::::::::::::::::::::::::::::::::::::
@echo off
cd "%~dp0"

:: start SpoutReceiver
start bin\SpoutReceiver

:: CLSID constants
set CLSID_LAVSplitterSource={B98D13E7-55DB-4385-A33D-09FD1BA26338}
set CLSID_LAVVideoDecoder={EE30215D-164F-4A92-A4EB-9D4C13390F9F}
set CLSID_SpoutGrabber={2060C516-38D1-4E4D-AD67-8CF6BE5FA859}
set CLSID_ColorSpaceConverter={1643E180-90F5-11CE-97D5-00AA0055595A}
set CLSID_VideoRendererDefault={6BC1CFFA-8FC1-4261-AC22-CFB4CC38DB50}

:: make sure that also LAV's DLLs are found
set PATH=filters;%PATH%

:: render 360p MP4 video, share texture with SpoutGrabber
bin\dscmd^
 -graph ^
%CLSID_LAVSplitterSource%;src=..\assets\bbb_360p_10sec.mp4;file=filters\LAVSplitter.ax,^
%CLSID_LAVVideoDecoder%;file=filters\LAVVideo.ax,^
%CLSID_SpoutGrabber%;file=SpoutGrabber.ax,^
%CLSID_ColorSpaceConverter%,^
%CLSID_VideoRendererDefault%^
!0:1,1:2,2:3,3:4^
 -winCaption "SpoutGrabber Demo (360p)"^
 -size 640,360^
 -loop -1^
 -i

echo.
pause
