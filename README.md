Alternative firmware for Cacazi doorbell
===
<img alt="the doorbell guts" src="doorbell_guts.jpg" style="width:300px">

This firmware is written for the ATTiny412 and requires the original MCU to be replaced.
The functionality is almost identical to the original, with added support for HT12 style remotes. 
The MCU is the rightmost 8 pins chip, pin 1 is the uppermost on the left.
The doorbell is powered from mains, so this modification is not recommended if you don't know what you're doing. The wires visible in the pictures were used for powering the doorbell while the firmware was under development. They *must* be removed and the box safely closed with screws *before * connecting the doorbell to a mains socket