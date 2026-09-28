#!/usr/bin/perl -w

# EXPECTED TO RUN FROM TOP LEVEL OF SPIKE

my $projectDir = "/data/ackley/PART4/code/D/MFMx-spikes";
my $fileIDSubDir = "build_cross/FileIDs.h";
my %idhash;

my $fileIDPath;

sub failOut {
    print("ENOMAT");
    exit(0);
}
sub setProjectDirs {
    $fileIDPath = "$projectDir/$fileIDSubDir";
}

sub parseFileIds {
    my $path = shift;
    open(my $hdl, "<$path") or die $!;
    my $q = '"';
    while (<$hdl>) {
        chomp;
        m!^ XX\(${q}/([^$q]+)$q,\s*(\d+)! or next;
        $idhash{$2} = $1;
    }
}

sub parseMFMXMark {
    my $mark = shift;
    # {44:18 9 1,2 hn ->TWIMC}
    #$mark =~ /^.(\d+):(\d+) \d+,\d+ ([^ \}]+).$/ or die "no match '$mark'";
    #$mark =~ /^\{(\d+):(\d+) \d+,\d+ (..) ?(.*)\}$/ or return undef;
    $mark =~ /^\{(\d+):(\d+) \d+ \d \d+,\d+ (..) ?(.*)\}$/ or return undef;
    return ($1,$2,$3,$4);
}

sub expandMark {
    my ($fid,$line,$hn,$note) = @_;
    exit(1) unless defined $hn;
    my $path = $idhash{$fid};
    exit(2) unless defined $path;
    return "$projectDir/$path:$line:$hn:$note";
}

my $srcdir = shift @ARGV or die "Usage: $0 SPIKEDIR MARK\n";
my $mark = shift @ARGV or die "Usage: $0 SPIKEDIR MARK\n";

setProjectDirs($srcdir);
parseFileIds($fileIDPath);

my $result = expandMark(parseMFMXMark("$mark"));
print($result);

