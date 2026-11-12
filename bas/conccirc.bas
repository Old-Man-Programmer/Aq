

5 PRINT CHR$(11)
10 PI=3.14159
20 FOR J=30 TO 2 STEP -2
30 R=J
40 FOR I=0 TO 2*PI STEP .1
50 X=R*COS(I)
60 Y=R*SIN(I)
70 PSET(40+X,40+Y)
80 NEXT I
90 NEXT J

REM See what happens when you change the values after STEP
REM in lines 20 and 40. 

RUN
