#!/usr/bin/perl -w
my $line = <>;
chomp($line);
$line =~ s/^\s+//; # chew off any leading spaces
my ($text,$data,$bss,$dec,$hex,$prog) = split(/\s+/,$line);
$prog =~ s!.*?/((([^/]+/){3})[^/]+)$!$1!; # keep last four parts of path (or all of it)
my $secs = `date '+%s'`;
chomp($secs);
my $readable = `date '+"%c"'`;
chomp($readable);
print("$secs $text $data $bss $dec $hex $prog $readable\n");

