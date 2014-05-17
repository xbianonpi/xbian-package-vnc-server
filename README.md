xbian-package-vnc-server
========================

VNC Server for Raspberry PI using dispmanx https://github.com/hanzelpeter/dispmanx_vnc.git

Because of RPI processing power, one should lower RPIs resolution. To use it with XBMC, 480p (hdmi_mode=4 in config.txt) 
is still enough for XBMC's screen and speed is acceptable to allow some real work.  


Cubox-i code is XBian development based on portions of Android (fastdroid) project. 
Supports MULTI BUFFERS as parameter supplied from command line.

TODO: If application configures new FB desktop with MULTI_BUFFER is changing imxvncserver crashes.