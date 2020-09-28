use v5.14;
use strict;
use warnings;

my @prime = map $_, 0..20_000;
@prime = grep { 2 <= $_; } @prime;

my $num = $prime[0];
while ( $num != $prime[-1] ) {
    @prime = grep {
        ($_ <= $num) or (($_ % $num) != 0);
    } @prime;

    $num = bigger_number( $num, \@prime );
}

# while ( my @numbers = splice(@prime, 0, 8) ) {
#     say join( ',', map sprintf("%5d", $_), @numbers );
# }

{
    my $fs = 48_000;
    my $param = 30;
    my $n = ($param / 340) * $fs; # 音速: 340m/s

    my $delay0 = bigger_number(int($n), \@prime);
    my @delay_samples = map {
        bigger_number(int($delay0 * $_), \@prime);
    } ( 240/100, 200/100, 160/100, 80/100, 330/100, 220/100 );

    say '// ', join( ', ', map sprintf("%4d", $_), @delay_samples );

    say '// ', join( ', ', map {
        my $posi = bigger_number(int($delay0 * $_), \@prime);
        sprintf("%4d", $posi);
    } (7/10, 17/10, 19/10, 23/10) );

    say '// ', join( ', ', map {
        my $posi = bigger_number(int($delay0 * $_), \@prime);
        sprintf("%4d", $posi);
    } (29/100, 31/100, 37/100, 23/100) );

    for my $i ( 1..64 ) {
        my $t = $i / 64;
        my $time_sec = 2 ** (-1.0 + (5.0 * $t));
        my $samples = int($fs * $time_sec);

        my @times_list = map {
            $samples / $_;
        } @delay_samples;

        # @gain_list[0..1] = map {
        #     2 ** (log2(0.001) / ($_ / 4));
        # } @times_list[0..1];

        printf("  { %s }, // [%2d] %.3f\n",
            join(', ', map {
                my $gain = 2 ** (log2(0.001) / $_);
                sprintf("0x%05X", int(($gain * 0x100_0000) + .5));
            } @times_list), $i, $time_sec );
    }
}

sub bigger_number {
    my ( $th, $numbers ) = @_;
    my @tmp = grep { $th < $_; } @{$numbers};
    return @tmp ? $tmp[0] : $th;
}

sub log2 {
    return log($_[0]) / log(2.0);
}
