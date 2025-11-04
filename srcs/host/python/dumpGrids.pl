#!/usr/bin/perl -w

my $clear_screen = `clear`;

my $buf = "";
my $findopen = 1;
my $count = 0;
my $buflines;
while (<>) {
    if ($findopen) {
        if (/.*?<<<(.*)$/) {
            $buf = "$1\n";
            $findopen = 0;
            $buflines = 0;
        }
    } elsif (/^(.*)>>>/) {
        if ($count++ % 5 == 0) {
            print("$clear_screen$buf");
        }
        $findopen = 1;
    } elsif (/^== (.*?)==.*$/) {
        if ($buflines < 75) {
            $buf .= $_;
            ++$buflines;
        }
    } # else discard
}
