#!/usr/bin/perl -w

# EXPECTED TO RUN FROM TOP LEVEL OF SPIKE

my $a2lprog = "/opt/tenstorrent/sfpi/compiler/bin/riscv32-tt-elf-addr2line";
my $a2loptsTemplate = "-f -e <PROJECTDIR>/build_cross/bin/<LCIMGNAME> -C <PC>";

my $projectDir = "/data/ackley/PART4/code/D/MFMx-spikes";
my $fileIDSubDir = "build_cross/FileIDs.h";
my %idhash;
my %namehash;

my $fileIDPath;

sub failOut {
    print("ENOMAT");
    exit(0);
}
sub setProjectDirs {
    $fileIDPath = "$projectDir/$fileIDSubDir";
    #print("P[$fileIDPath]");
}

sub getPCInfo {
    my $imgname = lc(shift);
    my $pc = shift;
    my $opts = $a2loptsTemplate;
    $opts =~ s/<LCIMGNAME>/$imgname/ or failOut();
    $opts =~ s/<PC>/$pc/ or failOut();
    $opts =~ s/<PROJECTDIR>/$projectDir/ or failOut();
    my $cmd = "$a2lprog $opts";
    #print($cmd."\n");
    my $res = `$cmd`;
    #$res .= "EXTRA\n";
    #print("FOTS{$res}");
    my ($func,$fidl) = split("\n",$res);
    #print("FOTS{$func}\n");
    #print("SNOTE{$fidl}\n");
    my @rs;
    for my $m ($fidl) {
        $m =~ m!^([^:]+):(\d+)([^d]|$)! or return "";
        push @rs,"$1\0$2\0$pc\0$func";
    }
    return @rs;
}

sub parseFileIds {
    my $path = shift;
    open(my $hdl, "<$path") or die $!;
    my $q = '"';
    while (<$hdl>) {
        chomp;
        m!^ XX\(${q}/([^$q]+)$q,\s*(\d+)! or next;
        $idhash{$2} = $1;
        $namehash{$1} = $2;
    }
    #print(join("\n--",keys(%namehash)));
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

my ( $imgname, $pc);
#print("@ARGV");
($imgname = shift @ARGV) and
    ($pc = shift @ARGV) and
    !defined(shift @ARGV) or
    die "Usage: $0 IMGNAME PC";

#print("my $imgname, $pc;\n");

setProjectDirs();
parseFileIds($fileIDPath);
my @fidls = getPCInfo($imgname,$pc);
chomp(@fidls);
#print("SDCONFD ".join(",,",@fidls)." spooge\n");
my @marks;
for my $fidl (@fidls) {
    my @l = split("\0",$fidl);
    next unless scalar(@l) == 4;
    my ($file,$line,$pc,$func) = @l;
    $file =~ s!^(.*?/MFMx-spikes/)!! or next;
    #print("FILE{$file}\n");
    my $fid = $namehash{$file};
    defined $fid or die "fid? '$file'\n";
    my $mark = "$fid:$line\n$func";
    push @marks,$mark;
}
print(join(";",@marks));
