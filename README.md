# neojam
a nwe version for JamVM1.0.0


## Build JamVM

neojam has been fixed some JamVM's compile error when using GCC4.X, one of them like this:

      dll_md.c:37:32: error: lvalue required as increment operand
      SCAN_SIG(sig, ((u8)apntr)++ = ((u8)opntr)++, *apntr++ = *opntr++);
      
The reason is that it uses some very old (and now omitted) GNU C extensions(cast-as-lvalue). A heavyweight but working approach is to modify the code manually, for instance, the above code can be rewritten as:

``` C
u8 *temp = (u8 *)apntr;
*temp = *((u8*)opntr)++;
temp++;
apntr = temp;
```

neojam has been fixed all the cast-as-lvalue.

Another fix is to remove some code with undefined behavior, one such example is:

``` C
*ostack++ = ostack[-1];
```

For details， see https://github.com/qc1iu/neojam/commit/659b01447245fe40a6725b7ef08da1dfbba283d9
