BEGIN { print "// GENERATED ".`date`."#pragma once\n#include \"itype.h\"\nnamespace MFM::T6 {\n" } 
END { print "}\n" } 
/^([0-9a-fA-F]+) .*?__(transportblock.*?)$/ && print "  static const u32 $2 = 0x$1;\n";

    
