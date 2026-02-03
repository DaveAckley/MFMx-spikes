BEGIN {
    my @nows = localtime();
    my $date = localtime();
    my $yr = $nows[5] - (2000-1900);
    my $lyr = length($yr);
    my $yday = $nows[7];
    my $lday = length($yday);
    my $mins = $nows[2]*60+$nows[1];
    my $lmins = length($mins);
    my $timestamp = "$lyr$yr.$lday$yday.$lmins$mins"; # LEXIMITED FUCK
    my $path = __FILE__;
    print <<"EOM";
/* 
GENERATED $date 
VERSION = "$timestamp"
BY $path
*/
#pragma once
#include "itype.h"
namespace MFM::T6 {
  inline const char * getMFMxModuleVersion() { return "MFMx-$timestamp"; }
EOM
} 
END { print "}\n" } 
/^([0-9a-fA-F]+) .*?__((transportblock|hostblock|imageblock).*?)$/ && print "  static const u32 $2 = 0x$1;\n";

    
