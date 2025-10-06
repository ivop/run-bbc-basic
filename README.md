# Run BBC Basic

At first, this is **not** a BBC emulator.
It only runs a small 6502 emulator, the BBC HiBasic 3.10 ROM, and a tiny 256 byte OS from 0xff00-0xffff to bootstrap the emulated machine.
Whenever BBC Basic does a MOS call, it is intercepted by the emulation loop, and its functionality is replicated in C code, after which it returns to Basic.

### What works?

Except for the graphics related functions and VDU in general, everything sort of works.
Even TIME works, so you can compare its speed to real hardware.
Star commands are passed to the shell, so you can do ```*ls```.
All paths can be standard host paths, like ```LOAD "test/FIBO.BAS"```.

### Escape?

Keyboard input is handled on a line-by-line basis (utilizing getline()) and there is no keyboard polling loop running in the background.
This means that Escape does not work like you might be used to on a real BBC.
To interrupt a running program, press CTRL-C, and type ```OLD``` to get your program back.
To exit Basic all together, press CTRL-Z.

### Free memory?

```
>@%=&20108:PRINT (HIMEM-LOMEM)/1024;" kB"
    44.0 kB
```
