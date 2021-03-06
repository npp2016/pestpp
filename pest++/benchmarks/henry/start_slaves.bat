REM set variables used in script
set nslaves=100
set pestpp_file=this

REM create directories for PEST++ and the slaves to run in
REM xcopy /e /q /y template master\
FOR /L %%i IN (1,1,%nslaves%) DO (
  xcopy /e /q /y template slave%%i\
)

REM start YAMR master
REM start /D"%CD%\master" .\pest++ %pestpp_file% /h :4006

REM start YAMR slaves
 FOR /L %%i IN (1,1,%nslaves%) DO (
   echo %%i
   start /D"%CD%\slave%%i" .\pest++ /H anole:4006
 )

