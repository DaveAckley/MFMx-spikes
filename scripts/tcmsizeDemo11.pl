#!/usr/bin/perl -W
use POSIX;

sub encPay2TCMs {
    my $payb = shift;
    return 0 if $payb <= 8;     # packet size 16B h+0..8+f
    return 1 if $payb <= 24;    # packet size 32B h+0(9)..24+f
    return 2 if $payb <= 40;    # packet size 64B h+0(25)..40+f

    # Then the pattern is

    # tcmsize = T>=3
    # packetSize = 64*(T-1)
    # segments = packetSize / 16
    # runs = (segments-4)/2
    # capacity = 28 + runs*32 + 12
    #          = 32*runs + 40
    # 0s: h4B p4B p4B p4B
    # 1s: p4B p4B p4B p4B  ==p28B

    # [
    # 0s: p4B p4B p4B p4B
    # 1s: p4B p4B p4B p4B
    # ] x runs
    #                      ==p32Bea

    # 0s: p4B p4B p4B a4B  
    # 1s: x4B x4B x4B f4B  ==p12B

    # and
    # T = segments/4 + 1

    # T = ((capacity-40)/16 + 4)/4 + 1
    # T = ((capacity-40)/64 + 1) + 1
    # T = (capacity-40)/64 + 2
    # T = (payb-40)/64 + 2, payb>44

    return ceil((($payb-40))/64) + 2; # if $payb > 44;
}

sub decTCMs2Pay {
    my $tcms = shift;
    return 8 if $tcms == 0;
    return 24 if $tcms == 1;
    return 40 if $tcms == 2;
    # (PX-40)/64+2 = TS     PX: max payload, TS: tcmsize
    # (PX-40)/64 = TS-2
    # (PX-40) = 64*(TS-2)
    # PX = 64*(TS-2)+40
    return ($tcms-2)*64+40;
}

sub tcmSize2PacketOverheadBytes {
    my $tcms = shift;
    return 8 if $tcms <= 1;  # h=4B + f=4B
    return 24; # h=4B + a=4B + last packet padding=12B + f=4B
}

sub decTCMs2Packet {
    my $tcms = shift;
    my $maxpay = decTCMs2Pay($tcms);
    my $overhead = tcmSize2PacketOverheadBytes($tcms);
    return $maxpay + $overhead;
}

for (my $payb = 0; $payb <= 16500; $payb = int(1.05*$payb+1)) {
    my $tcms = encPay2TCMs($payb);
    my $ival = decTCMs2Pay($tcms);
    my $abs = $ival - $payb;
    my $rel = int(100*(100*$abs/($payb+1)))/100.0;
    my $pkt = decTCMs2Packet($tcms);
    my $flag = ($abs==0) ? "<<<FULL":"";

    print("encPay2TCMs($payb) = $tcms; decTCMs2Pay($tcms) = $ival ($pkt) abs=$abs rel=$rel%  $flag\n");
}
