#!/usr/bin/perl -w

# Scan through data from the logic analyzer and split up into HF frames.

# Warning: crude, but works.  Wants to be Python.

# Fix: It would be really nice if we could check the 8 bit and 32 bit CRCs.

# This is an example of the data as it comes from the logic analyzer:
# Time [s],Value,Parity Error,Framing Error
# 0.0006894375,0x00,,Error
# 0.0007024375,0xF0,,
# 0.0007110625,0xF0,,
# 0.0007196875,0xF0,,
# 0.00073275,0x00,,Error
# 0.0007456875,0x00,,Error
# 0.000776,0xF0,,
# 0.0008193125,0xF0,,
# 0.0008625625,0x00,,Error
# 0.000905875,0x00,,Error
# 0.000949125,0x00,,Error
# 0.0009924375,0xF0,,
# 0.0010010625,0xF0,,
# 0.0010096875,0xF0,,
# 0.001018375,0x00,,Error
# 0.0108819375,0x00,,Error
# 0.0108949375,0xF0,,
# 0.0109035625,0xF0,,
# 0.0109121875,0xF0,,

# If the line from the logic analyzer is not recognized, it is
# discarded.  Otherwise, no bytes are discarded.  Each line is either
# a complete frame with data or it is bytes that could not be parsed
# and is preceeded by "garbage".  Certain logic analyzer lines are
# thrown away as useless or unparsable, but otherwise every byte is
# kept and reported in a valid HF packet or in a garbage line.

# The timestamp in each line refers to the time of the first byte in
# the line.

use strict;

my($state) = 'out of sync';
my($garbage);
my($current_timestamp);
my($frame) = '';
my($data) = '';
my($datalen) = 0;

while(<>) {
    chomp;

    if(/^(\d+.\d+),0x([0-9A-F]{2}),,/) {
	my($timestamp) = $1;
	my($hexpair) = $2;

	if(!defined($current_timestamp)) {
	    $current_timestamp = $timestamp;
	}

	if($state eq 'out of sync') {
	    if($hexpair eq 'AA') {
		if(defined($garbage)) {
		    print "garbage: $current_timestamp: $garbage\n";
		    undef($current_timestamp);
		}
		$garbage = '';
		$state = 'parsing header';
		$current_timestamp = $timestamp;
		$frame = $hexpair;
	    }
	    else {
		if(defined($garbage)) {
		    $garbage .= $hexpair;
		}
		else {
		    $garbage = $hexpair;
		}
	    }
	}
	elsif($state eq 'parsing header') {
	    if(length($frame) < 2*7) {
		$frame .= $hexpair;
	    }
	    elsif(length($frame) == 2*7) {
		$frame .= $hexpair;
		if($frame =~ /^[0-9A-F]{12}([0-9A-F]{2})[0-9A-F]{2}$/) {
		    my($hex_data_length) = $1;

		    if($hex_data_length eq '00') {
			print "$current_timestamp: $frame\n";
			undef($current_timestamp);
			$frame = '';
			$state = 'next frame';
		    }
		    else {
			$state = 'parsing data';
			$datalen = 2 * 4 * unpack('C', pack('H2', $hex_data_length));
		    }
		}
		else {
		    die "Frame is apparently bad, internal problem.";
		}
	    }
	    else {
		die "Somehow frame is ", length($frame), " bytes long, which is not expected.";
	    }
	}
	elsif($state eq 'parsing data') {
	    $frame .= $hexpair;
	    if(length($frame) == 2*8 + $datalen) {
		$state = 'reading CRC32';
	    }
	}
	elsif($state eq 'reading CRC32') {
	    $frame .= $hexpair;
	    if(length($frame) == 2*8 + $datalen + 2*4) {
		print "$current_timestamp: $frame\n";
		undef($current_timestamp);
		$frame = '';
		$datalen = 0;
		$state = 'next frame';
	    }
	}
	elsif($state eq 'next frame') {
	    if($hexpair eq 'AA') {
		$state = 'parsing header';
		$current_timestamp = $timestamp;
		$frame = $hexpair;
	    }
	    else {
		$state = 'out of sync';
		$garbage = $hexpair;
		$current_timestamp = $timestamp;
	    }
	}
	else {
	    die "Bad state: $state";
	}
    }
    elsif(/^Time \[s\],Value,Parity Error,Framing Error$/) {
	; # Description line -- may be ignored.
    }
    elsif(/^\d+(.\d+e-\d{2}|),0x[0-9A-F]{2},,(Error|)$/) {
	; # Error line or floating point or single digit -- may be ignored.
    }
    else {
	print STDERR "Unparsed line: $_\n";
    }
}

# Output remaining bytes.
if($state eq 'out of sync') {
    if(defined($garbage)) {
	print "garbage: $current_timestamp: $garbage\n";
    }
}
elsif($state eq 'parsing header') {
    print "short frame: $current_timestamp: $frame\n";
}
elsif($state eq 'parsing data') {
    print "short frame: $current_timestamp: $frame\n";
}
elsif($state eq 'reading CRC32') {
    print "short frame: $current_timestamp: $frame\n";
}
elsif($state eq 'next frame') {
    # Nothing to do.
    ;
}
else {
    die "Bad state: $state";
}
