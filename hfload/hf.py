import ctypes
import random
import sys
import time

from . import crc
from . import sha256

assert sys.version_info.major >= 3

# Fix: Drop "HF" from most class names.  The module is called "hf", that's enough.
# Fix: Think about how to capture traffic.  Need timestamps.
# Fix: Put the CRC32 stuff back in when we switch to direct access to the serial line.
# Fix: Test code should be written for this module.
# Fix: Don't forget the documentation.
# Fix: We may also want to keep track of serial line saturation.  How much of
#      the serial line bandwidth are we using?  How much clumping is happening?
#      Are we having any sort of serial line problem?

# Fix: Would it be a good idea to set up the USB stuff to accept
#      directly an HF_Frame object?

# talkusb() action types.
INIT = 0
SHUTDOWN = 1
SEND = 2
RECEIVE = 3
SEND_MAX = 4
RECEIVE_MAX = 5

# Operation codes from hf_protocol.h.
opcodes = {
    # Serial protocol operation codes (Second header byte)
    'OP_NULL': 0,
    'OP_ROOT': 1,
    'OP_RESET': 2,
    'OP_PLL_CONFIG': 3,
    'OP_ADDRESS': 4,
    'OP_READDRESS': 5,
    'OP_HIGHEST': 6,
    'OP_BAUD': 7,
    'OP_UNROOT': 8,

    'OP_HASH': 9,
    'OP_NONCE': 10,
    'OP_ABORT': 11,
    'OP_STATUS': 12,
    'OP_GPIO': 13,
    'OP_CONFIG': 14,
    'OP_STATISTICS': 15,
    'OP_GROUP': 16,
    'OP_CLOCKGATE': 17,
    
    # USB interface specific operation codes
    'OP_USB_INIT':          128, # Initialize USB interface details
    'OP_GET_TRACE':         129, # Send back the trace buffer if present
    'OP_LOOPBACK_USB':      130,
    'OP_LOOPBACK_UART':     131,
    'OP_DFU':               132, # Jump into the boot loader
    'OP_USB_SHUTDOWN':      133, # Initialize USB interface details
    'OP_DIE_STATUS':        134, # Die status. There are 4 die per ASIC
    'OP_GWQ_STATUS':        135, # Global Work Queue protocol status
    'OP_WORK_RESTART':      136, # Stratum work restart regime
    'OP_USB_STATS1':        137, # Statistics class 1
    'OP_USB_GWQSTATS':      138, # GWQ protocol statistics
    'OP_USB_NOTICE':        139, # Asynchronous notification event
    'OP_PING':              140, # Echo
    'OP_CORE_MAP':          141, # Return core map
    'OP_VERSION':           142, # Version information
    'OP_FAN':               143, # Set Fan Speed
    'OP_NAME':              144, # System name write/read
    'OP_USB_DEBUG':         255
}

opnames = {}
for opcode_name, opcode in opcodes.items():
    assert opcode not in opnames
    opnames[opcode] = opcode_name

known_opcodes = set(opcodes.keys())
known_opnames = set(opnames.keys())

# From hf_protocol.h.
PROTOCOL_USB_MAPPED_SERIAL = 0
PROTOCOL_GLOBAL_WORK_QUEUE = 1

# From hf_protocol.h
HF_NTIME_MASK = 0x0fff       # Mask for for ntime
# If this bit is set, search forward for other nonce(s)
HF_NONCE_SEARCH = 0x1000     # Search bit in candidate_nonce -> ntime
    
class HF_Error(Exception):
    pass

class HF_Thermal(Exception):
    pass

class HF_InternalError(HF_Error):
    pass

# Fix: Might be less confusing to have a single object which can
#      both send and receive -- and in the future we may want
#      some state, so it would be good if it was in one place.
# Fix: Maybe we want two queues.  One is a list of packets,
#      the other is the continuous stream of bytes.  Let's
#      say we want to abort everything that's going on.  Then,
#      we would like to kill the packets and immediately send
#      the OP_ABORT broadcast without losing the sync with
#      the dies.
# Fix: We also need a separate queueing function which does
#      not immediately send.  The reason is that we may have
#      occasion to set up many packets at once within the
#      state machine, but we don't want to do the USB back
#      and forth at that particular time.
class Send():
    def __init__(self, talkusb):
        self.talkusb = talkusb
        self.queue = []
        self.max_send = self.talkusb(SEND_MAX, None, 0)

    # Fix: Maybe schedule -> send, and send -> transmit.
    def schedule(self, byteslist):
        raise HF_Error("schedule() is temporarily out of service.")
        assert len(byteslist) == 0 or {x >= 0 and x < 256 for x in byteslist} == set([True])
        if byteslist != None and len(byteslist) > 0:
            self.queue = self.queue + byteslist

    def send(self, byteslist):
        assert len(byteslist) == 0 or {x >= 0 and x < 256 for x in byteslist} == set([True])
#        if byteslist != None and len(byteslist) > 0:
#            self.queue = self.queue + byteslist
        sendsize = 0
        mybyteslist = byteslist.copy()
        while len(mybyteslist) > 0:
            sendsize = min(self.max_send, len(mybyteslist))
#            sendstuff = ctypes.create_string_buffer(bytes(mybyteslist[0:sendsize]))
            sendstuff = mybyteslist[0:sendsize]
            mybyteslist = mybyteslist[sendsize:]
#            print("Send:send(): Queue: %d bytes  Sending %d bytes. Bytes: %s"
#                  % (len(self.queue), sendsize, ["0x%02x" % (x) for x in self.queue[0:sendsize]]))
            while len(sendstuff) > 0:
                sendstuff_package = sendstuff.copy() #ctypes.create_string_buffer(bytes(sendstuff))
                rslt = self.talkusb(SEND, sendstuff_package, sendsize)
                if rslt > 0:
#                self.queue = self.queue[rslt:]
                    sendstuff = sendstuff[rslt:]
                elif rslt == 0:
                    pass
                else:
                    raise HF_Error("Bad call trying to send using talkusb(): %d" % (rslt))
            # If we sent the maximum size, we need to send a zero
            # length packet so that the other side knows the send is
            # done.  (A surprising consequence of the design of USB.)
        if sendsize == self.max_send:
#                print("Send:send(): sending zero length packet.")
            empty = ctypes.create_string_buffer(0)
            self.talkusb(SEND, empty, 0);

##    def send(self, byteslist):
##        assert len(byteslist) == 0 or {x >= 0 and x < 256 for x in byteslist} == set([True])
##        if byteslist != None and len(byteslist) > 0:
##            self.queue = self.queue + byteslist
##        if len(self.queue) > 0:
##            sendsize = min(self.max_send, len(self.queue))
##            sendstuff = ctypes.create_string_buffer(bytes(self.queue[0:sendsize]))
###            print("Send:send(): Queue: %d bytes  Sending %d bytes. Bytes: %s"
###                  % (len(self.queue), sendsize, ["0x%02x" % (x) for x in self.queue[0:sendsize]]))
##            rslt = self.talkusb(SEND, sendstuff, sendsize)
##            if rslt > 0:
##                self.queue = self.queue[rslt:]
##            elif rslt == 0:
##                pass
##            else:
##                raise HF_Error("Bad call trying to send using talkusb(): %d" % (rslt))
##            # If we sent the maximum size, we need to send a zero
##            # length packet so that the other side knows the send is
##            # done.  (A surprising consequence of the design of USB.)
##            if sendsize == self.max_send:
###                print("Send:send(): sending zero length packet.")
##                empty = ctypes.create_string_buffer(0)
##                self.talkusb(SEND, empty, 0);

class Receive():
    def __init__(self, talkusb):
        self.talkusb = talkusb
        self.queue = []
        self.max_receive = self.talkusb(RECEIVE_MAX, None, 0)

    def receive(self):
#        print("Receive:receive() called.")
        # The Atmel USB code has an odd feature that it has to be
        # asked four times before it responds with the packet.  This
        # is true even if the requests are delayed for a second.  So
        # we ask until we get data or we asked four times.
        for i in range(4):
            buf = "" #ctypes.create_string_buffer(self.max_receive)
            rslt = self.talkusb(RECEIVE, buf, self.max_receive)
            buf = rslt
            rslt = len(buf)
#            print("Receive:receive(): got %d bytes" % (rslt))
            if rslt > 0:
                self.queue = self.queue + list(buf[0:rslt])
                break
            elif rslt == 0:
                pass
            else:
                raise HF_Error("Bad call trying to receive using talkusb(): %d" % (rslt))

    def read(self):
        result = self.queue
        self.queue = []
        return result

# Adapted from hf_protocol.h.
# Conversions for the ADC readings from GN on-chip sensors
def GN_CORE_VOLTAGE(a):
    assert a >= 0 and a < 2**8
    return (float(a)/float(256))*1.2

def GN_DIE_TEMPERATURE(a):
    assert a >= 0 and a < 2**16
    return (float(a)*float(240))/4096.0 - 61.5

class Garbage():
    def __init__(self, garbage):
        self.garbage = garbage

    def read(self):
        return self.garbage

def check_framebytes(framebytes):
    assert {x >= 0 and x < 256 for x in framebytes} == set([True])
    assert len(framebytes) >= 8
    assert framebytes[0] == 0xaa
    assert framebytes[7] == crc.crc8(framebytes[1:7])
    if framebytes[6] == 0:
        assert len(framebytes) == 8
    else:
        data_length = 4 * framebytes[6]
        # Eight byte frame header, data, plus 4 crc32 bytes.
        # Fix: Restore when using serial line directly
        # expected_framebytes_length = 8 + data_length + 4
        expected_framebytes_length = 8 + data_length
        assert expected_framebytes_length == len(framebytes)
        data = framebytes[8:8+data_length]
# Fix: Restore when using serial line directly
#        crc32 = framebytes[-4:]
#        if crc32 != crc.crc32_to_bytelist(crc.crc32(data)):
#            raise HF_Error("Bad CRC32 checksum.")

def lebytes_to_int(lebytes):
    assert {x >= 0 and x < 256 for x in lebytes} == set([True])
    accum = 0
    pow = 1
    while lebytes:
        accum = accum + pow * lebytes[0]
        pow = 256 * pow
        lebytes = lebytes[1:]
    return accum

def int_to_lebytes(integer, digit):
    assert digit > 0
    assert integer >= 0 and integer < 256 ** digit
    result = []
    for i in range(digit):
        result = result + [integer % 256]
        integer = integer >> 8
    return result

# Fix: Document terminology: frame is the whole thing and consists of up to
#      three parts: the header, the data, and the CRC32 checksum.
# Fix: Wants to verify checksums and throw exception if they are not right.
#      And check for 0xaa.
# Fix: Wants to make all the fields of the header accessible, but also provide raw bytes.
# Fix: Should be able to initialize with stream of bytes or by filling in fields
#      and asking for the bytes.  Throw exception if field values are out of bounds.
# Fix: Maybe want something which checks for known opcode and whether fields are
#      plausible for that opcode -- problem is that if we are using this to report
#      what was seen on the wire, we need to make those assumptions, maybe.
# Fix: The really pure way to do this is to create a subclass for every opcode type
#      and then have specific methods for that type.  Probably more trouble than
#      its worth, but it would also let us have specific methods for parameters
#      that just occupy a couple bits.
class HF_Frame():
    def __init__(self, initial_state):
        self.initialize()
        if initial_state is None:
            pass
        elif isinstance(initial_state, list):
            self.off_the_wire(initial_state)
        elif isinstance(initial_state, dict):
            self.buildframe(initial_state)
        else:
            raise HF_Error("Argument type not supported: %s" % (inital_state))

    def initialize(self):
        self.framebytes = []
        self.operation_code = None
        self.chip_address = 0
        self.core_address = 0
        self.hdata = 0
        self.data_length_field = 0
        self.crc8 = 0
        self.data = None
# Fix: Restore when using serial line directly
#        self.crc32 = None
        self.data_length = 0;

    def off_the_wire(self, framebytes):
        check_framebytes(framebytes)
        self.framebytes = framebytes
        self.operation_code = framebytes[1]
        self.chip_address = framebytes[2]
        self.core_address = framebytes[3]
        self.hdata = lebytes_to_int(framebytes[4:6])
        self.data_length_field = framebytes[6]
        self.data_length = 4 * self.data_length_field
        self.crc8 = framebytes[7]
        if self.data_length > 0:
            assert {x >= 0 and x < 256 for x in framebytes} == set([True])
            self.data = framebytes[8:8+self.data_length]
# Fix: Restore when using serial line directly
#            self.crc32 = framebytes[8+self.data_length:]

    def construct_framebytes(self):
        frameheader = [0xaa, self.operation_code, self.chip_address, self.core_address] + \
            int_to_lebytes(self.hdata, 2) + [self.data_length_field, self.crc8]
        if self.data_length > 0:
# Fix: Restore when using serial line directly
#            return frameheader + self.data + crc.crc32_to_bytelist(self.crc32)
            return frameheader + self.data
        else:
            return frameheader

    def buildframe(self, framedict):
        legal_fields = set(['operation_code', 'chip_address', 'core_address', 'hdata', 'data'])
        received_fields = set(framedict.keys())
        assert received_fields.issubset(legal_fields)
        assert 'operation_code' in framedict
        assert framedict['operation_code'] in opnames
        self.operation_code = framedict['operation_code']
        if 'chip_address' in framedict:
            if framedict['chip_address'] < 0 or framedict['chip_address'] > 255:
                raise HF_Error("chip_address is out of range: %d" % (framedict['chip_address']))
            self.chip_address = framedict['chip_address']
        if 'core_address' in framedict:
            if framedict['core_address'] < 0 or framedict['core_address'] > 255:
                raise HF_Error("core_address is out of range: %d" % (framedict['core_address']))
            self.core_address = framedict['core_address']
        if 'hdata' in framedict:
            if framedict['hdata'] < 0 or framedict['hdata'] > 65535:
                raise HF_Error("hdata is out of range: %d" % (framedict['hdata']))
            self.hdata = framedict['hdata']
        if 'data' in framedict:
            assert len(framedict['data']) == 0 or {x >= 0 and x < 256 for x in framedict['data']} == set([True])
            assert len(framedict['data']) <= 1020 and len(framedict['data']) % 4 == 0
            if len(framedict['data']) > 0:
                self.data = framedict['data']
                self.data_length = len(framedict['data'])
                self.data_length_field = int(self.data_length / 4)
                # Fix: Restore when using serial line directly
                # self.crc32 = crc.crc32(self.data)
        crc8_input = [self.operation_code, self.chip_address, self.core_address] + int_to_lebytes(self.hdata, 2) \
            + [self.data_length_field]
        self.crc8 = crc.crc8(crc8_input)
        self.framebytes = self.construct_framebytes()

    def print(self):
        print("framebytes: %s" % (self.framebytes))
        print("operation_code: 0x%02x" % (self.operation_code))
        print("chip_address: 0x%02x" % (self.chip_address))
        print("core_address: 0x%02x" % (self.core_address))
        print("hdata: %d" % (self.hdata))
        print("data_length_field: %d" % (self.data_length))
        print("data: %s" % (self.data))


# Imitates "struct hf_hash_serial" in hf_protocols.h.
class hf_hash_serial():
    def __init__(self, midstate, merkle_residual, timestamp, bits, starting_nonce,
                 nonce_loops, ntime_loops, search_difficulty, option, group, spare3):
        assert len(midstate) == 32
        assert {x >= 0 and x < 256 for x in midstate} == set([True])
        assert len(merkle_residual) == 4
        assert {x >= 0 and x < 256 for x in merkle_residual} == set([True])
        assert timestamp >= 0 and timestamp < 2**32
        assert bits >= 0 and bits < 2**32
        assert starting_nonce >= 0 and starting_nonce < 2**32
        assert nonce_loops >= 0 and nonce_loops < 2**32
        assert ntime_loops >= 0 and ntime_loops < 2**16
        assert search_difficulty >= 0 and search_difficulty < 256
        assert option >= 0 and option < 256
        assert group >= 0 and option < 256
        assert len(spare3) == 3
        assert {x >= 0 and x < 256 for x in spare3} == set([True])

        self.midstate = midstate
        self.merkle_residual = merkle_residual
        self.timestamp = timestamp
        self.bits = bits
        self.starting_nonce = starting_nonce
        self.nonce_loops = nonce_loops
        self.ntime_loops = ntime_loops
        self.search_difficulty = search_difficulty
        self.option = option
        self.group = group
        self.spare3 = spare3
        self.generate_frame_data()

    def generate_frame_data(self):
        self.frame_data = self.midstate + self.merkle_residual + \
            int_to_lebytes(self.timestamp, 4) + int_to_lebytes(self.bits, 4) + \
            int_to_lebytes(self.starting_nonce, 4) + int_to_lebytes(self.nonce_loops, 4) + \
            int_to_lebytes(self.ntime_loops, 2) + [self.search_difficulty] + \
            [self.option] + [self.group] + self.spare3

# Fix: We would like to confirm that chip_address and core_address make sense
#      for our particular hardware, but that information is not available
#      to this object.
class HF_OP_HASH(HF_Frame):
    def __init__(self, chip_address, core_address, sequence, job):
        assert chip_address >= 0 and chip_address < 256
        assert core_address >= 0 and core_address < 256
        assert sequence >= 0 and sequence < 2**16
        assert isinstance(job, hf_hash_serial)
        self.job = job
        HF_Frame.__init__(self, {'operation_code': opcodes['OP_HASH'],
                                      'chip_address': chip_address,
                                      'core_address': core_address,
                                      'hdata': sequence,
                                      'data': self.job.frame_data})

# Imitates "strudct hf_candidate_nonce" in hf_protocols.h.
class hf_candidate_nonce:
    def __init__(self, nonce_bytes):
        assert len(nonce_bytes) == 8
        self.nonce = lebytes_to_int(nonce_bytes[0:4])
        self.sequence = lebytes_to_int(nonce_bytes[4:6])
        self.ntime = lebytes_to_int(nonce_bytes[6:8])
        self.ntime_offset = self.ntime & HF_NTIME_MASK
        self.search_forward = self.ntime & HF_NONCE_SEARCH

class HF_OP_NONCE(HF_Frame):
    def __init__(self, framebytes):
        HF_Frame.__init__(self, framebytes)
        assert len(self.data) % 8 == 0
        self.nonces = []
        for i in range(int(len(self.data) / 8)):
            self.nonces = self.nonces + [hf_candidate_nonce(self.data[8*i:8*i+8])]

# Imitates "struct hf_g1_monitor" in hf_protocols.h.
class hf_g1_monitor():
    def __init__(self, monitor_bytes):
        raw_temp = lebytes_to_int(monitor_bytes[0:2])
        self.die_temperature = GN_DIE_TEMPERATURE(raw_temp)
        self.core_voltage_main = GN_CORE_VOLTAGE(monitor_bytes[2])
        self.core_voltage_A = GN_CORE_VOLTAGE(monitor_bytes[3])
        self.core_voltage_B = GN_CORE_VOLTAGE(monitor_bytes[4])
        self.core_voltage_C = GN_CORE_VOLTAGE(monitor_bytes[5])
        self.core_voltage_D = GN_CORE_VOLTAGE(monitor_bytes[6])
        self.core_voltage_E = GN_CORE_VOLTAGE(monitor_bytes[7])

# Fix: Support all fields.
# Fix: Error check.
# Fix: Not sure how to handle this.  The monitoring values are
#      interpreted differently depending on what OP_CONFIG told the
#      die to do.  This information is not in the OP_STATUS packet.
#      For the moment we assume the "tachometer option" is not
#      used.  See page 33 of the GN Protocol Guide.  Guess we need
#      to support both interpretations and let the caller decide
#      which one is in effect.
# Fix: Figure out why most of the voltages are not present in OP_STATUS.
#      Perhaps I have to send out my own OP_CONFIG?
# Fix: We would like to decode the core map here, but this object does
#      not actually know how many cores there are.
class HF_OP_STATUS(HF_Frame):
    def __init__(self, initial_state):
        HF_Frame.__init__(self, initial_state)
        self.thermal_cutoff = (self.core_address & 0x80) >> 7
        self.tach_csec = self.core_address & 0x0f
        self.last_sequence_number = self.hdata
        self.monitor_data = hf_g1_monitor(self.data[0:16])
        self.coremap = self.data[8:]

# Fix: Support all fields.
# Fix: Error check.
class HF_OP_USB_INIT(HF_Frame):
    pass

class HF_OP_USB_SHUTDOWN(HF_Frame):
    pass

# Modeled on struct hf_usb_notice_data in hf_protocol.h.
class HF_OP_USB_NOTICE(HF_Frame):
    def __init__(self, initial_state):
        HF_Frame.__init__(self, initial_state)
        self.notification_code = self.hdata
        self.extra_data = None
        self.message = None
        if self.data_length_field > 0:
            self.extra_data = lebytes_to_int(self.data[0:4])
        if self.data_length_field > 1:
            try:
                raw_message = self.data[4:]
                first_NUL = raw_message.index(0)
            except ValueError:
                # Fix: Check that the last bytes are all NUL, there may be more than
                #      one, once the firmware is fixed to do that.
                raise HF_Error("OP_USB_NOTICE returned a non-NUL terminated string.")
            self.message = "".join([chr(x) for x in raw_message[0:first_NUL]])

# Fix: Returns a full frame or garbage that could not be parsed.
class HF_Parse():
    def __init__(self):
        self.state = 'out of sync'
        self.tokens = []
        self.clear_frame()

    def clear_frame(self):
        self.garbage = []
        self.frame_header = []
        self.data_length = 0
        self.frame_data = []
        # Fix: Restore when using serial line directly
        # self.frame_crc32 = []

    def input(self, rawbytes):
        for byte in rawbytes:
            if self.state == 'out of sync':
                if byte == 0xaa:
                    if len(self.garbage) > 0:
                        self.tokens = self.tokens + [Garbage(self.garbage)]
                    self.clear_frame()
                    self.frame_header = [0xaa]
                    self.state = 'parsing header';
                else:
                    self.garbage = self.garbage + [byte]
            elif self.state == 'parsing header':
                if len(self.frame_header) < 7:
                    self.frame_header = self.frame_header + [byte]
                elif len(self.frame_header) == 7:
                    if byte == crc.crc8(self.frame_header[1:]):
                        self.frame_header = self.frame_header + [byte]
                        self.data_length = 4 * self.frame_header[6]
                        if self.data_length == 0:
                            # Fix: This parallels code with the data, maybe this should
                            #      be a single "queue up what we got" method?
                            next_token = None
                            if self.frame_header[1] == opcodes['OP_NONCE']:
                                next_token = HF_OP_NONCE(self.frame_header)
                            elif self.frame_header[1] == opcodes['OP_STATUS']:
                                next_token = HF_OP_STATUS(self.frame_header)
                            elif self.frame_header[1] == opcodes['OP_USB_INIT']:
                                next_token = HF_OP_USB_INIT(self.frame_header)
                            elif self.frame_header[1] == opcodes['OP_USB_NOTICE']:
                                next_token = HF_OP_USB_NOTICE(self.frame_header)
                            else:
                                next_token = HF_Frame(self.frame_header)
                            self.tokens = self.tokens + [next_token]
                            self.clear_frame()
                            self.state = 'next frame'
                        else:
                            self.state = 'parsing data'
                    else:
                        # CRC8 does not match, bad frame header, so garbage.
                        self.garbage = self.frame_header + [byte]
                        self.frame_header = []
                        self.clear_frame()
                        self.state = 'out of sync'
                else:
                    raise HF_InternalError("Somehow frame is %d bytes long, more than "
                                           "should be possible." % (len(self.frame)))
            elif self.state == 'parsing data':
                self.frame_data = self.frame_data + [byte]
                # Fix: Restore when using serial line directly
#                if len(self.frame_data) == self.data_length:
#                    self.state = 'reading CRC32'
#                if len(self.frame_data) == self.data_length:
#                    self.state = 'reading CRC32'
                # Fix: This should be removed when using CRC32 check again.
                if len(self.frame_data) == self.data_length:
                    next_token = None
                    if self.frame_header[1] == opcodes['OP_NONCE']:
                        next_token = HF_OP_NONCE(self.frame_header + self.frame_data)
                    elif self.frame_header[1] == opcodes['OP_STATUS']:
                        next_token = HF_OP_STATUS(self.frame_header + self.frame_data)
                    elif self.frame_header[1] == opcodes['OP_USB_INIT']:
                        next_token = HF_OP_USB_INIT(self.frame_header + self.frame_data)
                    elif self.frame_header[1] == opcodes['OP_USB_NOTICE']:
                        next_token = HF_OP_USB_NOTICE(self.frame_header + self.frame_data)
                    else:
                        next_token = HF_Frame(self.frame_header + self.frame_data)
                    self.tokens = self.tokens + [next_token]
                    self.clear_frame()
                    self.state = 'next frame'
                elif len(self.frame_data) > self.data_length:
                    raise HF_InternalError("Length of self.frame_data (%d) > self.data_length (%d)" %
                                           (len(self.frame_data), self.data_length))
                # Fix: Restore when using serial line directly
#            elif self.state == 'reading CRC32':
#                self.frame_crc32 = self.frame_crc32 + [byte]
#                if len(self.frame_crc32) == 4:
#                    expected_crc32 = crc.crc32_to_bytelist(crc.crc32(self.frame_data))
#                    if self.frame_crc32 == expected_crc32:
#                        self.tokens = self.tokens + [HF_Frame(self.frame_header + self.frame_data + self.frame_crc32)]
#                        self.clear_frame()
#                        self.state = 'next frame'
#                    else:
#                        self.tokens = self.tokens + [Garbage(self.frame_header + self.frame_data + self.frame_crc32)]
#                        self.clear_frame()
#                        self.state = 'out of sync'
#                elif len(self.frame_crc32) > 4:
#                    raise HF_InternalError("%d bytes in CRC32 field.  Should not happen." % (len(self.frame_crc32)))
            elif self.state == 'next frame':
                if byte == 0xaa:
                    self.frame_header = [0xaa]
                    self.state = 'parsing header';
                else:
                    self.garbage = [byte]
                    self.state = 'out of sync'
            else:
                raise HF_InternalError("Bad parser state: %s" % (self.state))

    def has_token(self):
        if len(self.tokens) > 0:
            return True
        else:
            return False

    def next_token(self):
        if len(self.tokens) > 0:
            next = self.tokens[0]
            self.tokens = self.tokens[1:]
            return next
        else:
            return None

def dice_up_coremap(lebytes, dies, cores):
    assert len(lebytes) % 4 == 0
    assert 8 * len(lebytes) >= dies * cores
    assert dies > 0 and cores > 0
    die_modulus = 2 ** cores
    global_bitmap = lebytes_to_int(lebytes)
    die_coremaps = []
    for die in range(dies):
        single_die_map = coremap_array(global_bitmap % die_modulus, cores) 
        die_coremaps = [single_die_map] + die_coremaps
        global_bitmap = global_bitmap >> cores
    return die_coremaps

def coremap_array(die_bitmap, cores):
    result = []
    mask = 0x1
    for i in range(cores):
        if die_bitmap & mask > 0:
            result = result + [1]
        else:
            result = result + [0]
        mask = mask << 1
    return result

def display_cores_by_G1_location(ca, printer):
    for line in display_cores_by_G1_location_lines(ca):
        printer(line)

def display_cores_by_G1_location_lines(ca):
    def I(yesno):
        if yesno > 0:
            return 'x'
        else:
            return ' '
    def NI(yesno):
        if yesno > 0:
            return 'X'
        else:
            return ' '        
    assert len(ca) == 96
    return ["".join([NI(ca[95]),  I(ca[74]), NI(ca[73]),  I(ca[53]), NI(ca[52]),  I(ca[33]), NI(ca[32]),  I(ca[11]), NI(ca[10])]),
            "".join([ I(ca[94]), NI(ca[75]),  I(ca[72]), NI(ca[54]),  I(ca[51]), NI(ca[34]),  I(ca[31]), NI(ca[12]),  I(ca[9])]),
            "".join([NI(ca[93]),  I(ca[76]), NI(ca[71]),  I(ca[55]), NI(ca[50]),  I(ca[35]), NI(ca[30]),  I(ca[13]), NI(ca[8])]),
            "".join([ I(ca[92]), NI(ca[77]),  I(ca[70]), NI(ca[56]),  I(ca[49]), NI(ca[36]),  I(ca[29]), NI(ca[14]),  I(ca[7])]),
            "".join([NI(ca[91]),  I(ca[78]), NI(ca[69]),  I(ca[57]), NI(ca[48]),  I(ca[37]), NI(ca[28]),  I(ca[15]), NI(ca[6])]),
            "".join([ I(ca[90]), NI(ca[79]),  I(ca[68]), NI(ca[58]),  I(ca[47]), NI(ca[38]),  I(ca[27]), NI(ca[16]),  I(ca[5])]),
            "".join([NI(ca[89]),  I(ca[80]), NI(ca[67]),  I(ca[59]), NI(ca[46]),  I(ca[39]), NI(ca[26]),  I(ca[17]), NI(ca[4])]),
            "".join([ I(ca[88]), NI(ca[81]),  I(ca[66]), NI(ca[60]),  I(ca[45]), NI(ca[40]),  I(ca[25]), NI(ca[18]),  I(ca[3])]),
            "".join([NI(ca[87]),  I(ca[82]), NI(ca[65]),  I(ca[61]), NI(ca[44]),  I(ca[41]), NI(ca[24]),  I(ca[19]), NI(ca[2])]),
            "".join([ I(ca[86]), NI(ca[83]),  I(ca[64]), NI(ca[62]),  I(ca[43]), NI(ca[42]),  I(ca[23]), NI(ca[20]),  I(ca[1])]),
            "".join([NI(ca[85]),  I(ca[84]), NI(ca[63]),      'O',          'O',        'O', NI(ca[22]),  I(ca[21]), NI(ca[0])])]

# Fix: This really needs test code and documentation.  Make sure it
#      works for future designs as well as G1.
def decode_op_status_job_map(jobmap, cores):
    assert 8 * len(jobmap) <= 2 * cores
    bitmap = lebytes_to_int(jobmap)
    active_map = [0] * cores
    for i in range(cores):
        if bitmap & (1 << 2*i) > 0:
            active_map[i] = 1
    pending_map = [0] * cores
    for i in range(cores):
        if bitmap & (1 << (2*i + 1)) > 0:
            pending_map[i] = 1
    return [active_map, pending_map]

# Fix: Remove this if we don't need it.
# Find empties: takes core list like [0,1,0,0,1...] and converts to
# dictionary in which each key is an empty slot.
def core_list_to_dict(corelist):
    empties = {}
    for i in range(len(corelist)):
        if corelist[i] == 0:
            empties[i] = 1
    return empties

# Fix: cores -> slots
def list_available_cores(corelist):
    empties = []
    for i in range(len(corelist)):
        if corelist[i] == 0:
            empties.append(i)
    return empties

def random_work(search_difficulty):
    midstate = list(randbytes(32))
    merkle_residual = list(randbytes(4))
    timestamp = lebytes_to_int(randbytes(4))
    bits = lebytes_to_int(randbytes(4))
    return hf_hash_serial(midstate, merkle_residual, timestamp, bits, 0, 0, 0, search_difficulty, 0, 0, [0, 0, 0])

def randbytes(count, source="/dev/urandom"):
    src = open(source, "rb")
    rslt = src.read(count)
    src.close()
    return rslt

import hashlib

# version: integer
# previous_block_hash: 32 bytes in order specified by FIPS-180-4
#   (Sometimes called "big-endian".)
# merkle_root_hash: 32 bytes in order specified by FIPS-180-4
#   (Sometimes called "big-endian".)
# time: integer
# bits: integer
# nonce: integer
#def compute_block_hash(version, previous_block_hash, merkle_root_hash, time, bits, nonce):
#    hash_input = prepare_block_hash_input(version, previous_block_hash, merkle_root_hash, time, bits, nonce)
    # Fix: Debugging.
#    print("hash_input: %s" % (hash_input))
#    hash1 = hashlib.sha256(bytes(hash_input)).digest()
#    hash2 = hashlib.sha256(hash1).digest()
#    return hash2
def compute_block_hash(version, previous_block_hash, merkle_root_hash, time, bits, nonce):
    assert version >= 0 and version < 2**32
    assert len(previous_block_hash) == 32
    assert {x >= 0 and x < 256 for x in previous_block_hash} == set([True])    
    assert len(merkle_root_hash) == 32
    assert {x >= 0 and x < 256 for x in merkle_root_hash} == set([True])
    assert time >=0 and time < 2**32
    assert bits >=0 and bits < 2**32
    assert nonce >= 0 and nonce < 2**32
    version_bytes = int_to_lebytes(version, 4)
    time_bytes = int_to_lebytes(time, 4)
    bits_bytes = int_to_lebytes(bits, 4)
    nonce_bytes = int_to_lebytes(nonce, 4)
    hash_input = version_bytes + previous_block_hash + merkle_root_hash + time_bytes + bits_bytes + nonce_bytes
    hash1 = hashlib.sha256(bytes(hash_input)).digest()
    hash2 = hashlib.sha256(hash1).digest()
    return hash2

def prepare_block_hash_input(version, previous_block_hash, merkle_root_hash, time, bits, nonce):
    assert version >= 0 and version < 2**32
    assert len(previous_block_hash) == 32
    assert {x >= 0 and x < 256 for x in previous_block_hash} == set([True])    
    assert len(merkle_root_hash) == 32
    assert {x >= 0 and x < 256 for x in merkle_root_hash} == set([True])
    assert time >=0 and time < 2**32
    assert bits >=0 and bits < 2**32
    assert nonce >= 0 and nonce < 2**32
    version_bytes = int_to_lebytes(version, 4)
    pbh_bytes = list(previous_block_hash).copy()
    pbh_bytes.reverse()
    mrh_bytes = list(merkle_root_hash).copy()
    mrh_bytes.reverse()
    time_bytes = int_to_lebytes(time, 4)
    bits_bytes = int_to_lebytes(bits, 4)
    nonce_bytes = int_to_lebytes(nonce, 4)
    hash_input = version_bytes + pbh_bytes + mrh_bytes + time_bytes + bits_bytes + nonce_bytes
    return hash_input

import binascii

#def compute_midstate(version, previous_block_hash, merkle_root_hash):
#    midstate_raw_input = prepare_block_hash_input(version, previous_block_hash, merkle_root_hash, 0x00, 0x00, 0x00)
#    midstate_input = midstate_raw_input[0:64]
#    print("midstate_input: %s" % (binascii.hexlify(bytes(midstate_input))))
#    hash_result = hashlib.sha256(bytes(midstate_input)).digest()
#    return hash_result

def compute_midstate(version, previous_block_hash, merkle_root_hash):
    assert version >= 0 and version < 2**32
    assert len(previous_block_hash) == 32
    assert {x >= 0 and x < 256 for x in previous_block_hash} == set([True])    
    assert len(merkle_root_hash) == 32
    assert {x >= 0 and x < 256 for x in merkle_root_hash} == set([True])
    version_bytes = int_to_lebytes(version, 4)
    block_begin = version_bytes + previous_block_hash + merkle_root_hash
    hash_input = block_begin[0:64]
    hash1 = hashlib.sha256(bytes(hash_input)).digest()
    hash2 = hashlib.sha256(hash1)
    return hash2.digest()

def cgminer_flip64(sixty_four):
    assert len(sixty_four) == 64
    result = []
    for i in range(16):
        a = sixty_four[4*i:4*i+4]
        b = a.copy()
        b.reverse()
        result = result + b
    return result

def cgminer_calc_midstate(sixty_four):
    assert len(sixty_four) == 64
    sixty_four_flipped = cgminer_flip64(sixty_four)
    return hashlib.sha256(bytes(sixty_four_flipped)).digest()

def flip32(thirty_two):
    assert len(thirty_two) == 32
    assert {x >= 0 and x < 256 for x in thirty_two} == set([True])
    result = []
    for i in range(8):
        a = thirty_two[4*i:4*i+4]
        a.reverse()
        result = result + a
    return result

# Counts leading zero bits from most significant bit to the least significant.
def count_leading_zero_bits(byte):
    assert byte >= 0 and byte < 256
    zero_bits = 0
    mask = 0x80
    while mask > 0:
        if mask & byte == 0:
            zero_bits = zero_bits + 1
        else:
            break
        mask = mask >> 1
    return zero_bits

# Counts zero bits in this way.  Zero bits within the byte are counted
# from most significant to least significant.  It's like taking the
# bytes, then converting them to bits from least significant to most
# significant in the usual way.  Then reverse this string of bits and
# count the zero bits from the beginning.
def count_leading_zeros(bytelist):
    assert {x >= 0 and x < 256 for x in bytelist} == set([True])
    zero_bits = 0
    for i in range(len(bytelist)-1, -1, -1):
        zero_bits = zero_bits + count_leading_zero_bits(bytelist[i])
        if bytelist[i] > 0:
            break
    return zero_bits

def sequence_a_le_b(a, b):
    assert a >= 0 and a < 2**16
    assert b >= 0 and b < 2**16
    if abs(a - b) < 32768:
        return a < b
    else:
        return b < a

def sequence_a_leq_b(a, b):
    assert a >= 0 and a < 2**16
    assert b >= 0 and b < 2**16
    if a == b:
        return True
    else:
        return sequence_a_le_b(a, b)

def rand_job(rnd):
    newjob = {}
    newjob['version'] = 2
    newjob['previous block hash'] = list(rnd.read(32))
    newjob['merkle tree root'] = list(rnd.read(32))
    newjob['timestamp'] = lebytes_to_int(list(rnd.read(4)))
    newjob['bits'] = lebytes_to_int(list(rnd.read(4)))
    newjob['starting nonce'] = 0
    newjob['nonce loops'] = 0
    newjob['ntime loops'] = 0
    return newjob    

def check_nonce(job, nonce, zerobits_required):
    assert check_job(job)
    assert nonce >= 0 and nonce < 4294967296 # 32 bits
    assert zerobits_required >= 0 and zerobits_required < 256
    feed_to_regen_hash = int_to_lebytes(job['version'], 4) + \
        job['previous block hash'] + \
        job['merkle tree root'] + \
        int_to_lebytes(job['timestamp'], 4) + \
        int_to_lebytes(job['bits'], 4) + \
        int_to_lebytes(nonce, 4)
    regen_hash = sha256.cgminer_regen_hash(feed_to_regen_hash)
    regen_hash_expanded = list(regen_hash)
    zerobits = count_leading_zeros(regen_hash_expanded)
    if zerobits >= zerobits_required:
        return True
    else:
        return False

def check_nonce_work(job, nonce):
    assert check_job(job)
    assert nonce >= 0 and nonce < 4294967296 # 32 bits
    feed_to_regen_hash = int_to_lebytes(job['version'], 4) + \
        job['previous block hash'] + \
        job['merkle tree root'] + \
        int_to_lebytes(job['timestamp'], 4) + \
        int_to_lebytes(job['bits'], 4) + \
        int_to_lebytes(nonce, 4)
    regen_hash = sha256.cgminer_regen_hash(feed_to_regen_hash)
    regen_hash_expanded = list(regen_hash)
    zerobits = count_leading_zeros(regen_hash_expanded)
    return [zerobits, regen_hash_expanded]

def check_job(job):
    exact_job_fields = set(['version', 'previous block hash', 'merkle tree root', 'timestamp', 'bits', 'starting nonce', 'nonce loops', 'ntime loops'])
    if set(job.keys()) != exact_job_fields:
        return False
    two_to_32 = 0x1 << 32
    two_to_16 = 0x1 << 16
    if not(job['version'] >= 0 and job['version'] < two_to_32):
        return False
    if not({x >= 0 and x < 256 for x in job['previous block hash']} == set([True])):
        return False
    if not({x >= 0 and x < 256 for x in job['merkle tree root']} == set([True])):
        return False
    if not(job['timestamp'] >= 0 and job['timestamp'] < two_to_32):
        return False
    if not(job['bits'] >= 0 and job['bits'] < two_to_32):
        return False
    if not(job['starting nonce'] >= 0 and job['starting nonce'] < two_to_32):
        return False
    if not(job['nonce loops'] >= 0 and job['nonce loops'] < two_to_32):
        return False
    if not(job['ntime loops'] >= 0 and job['ntime loops'] < two_to_16):
        return False
    return True

def prepare_hf_hash_serial(job, search_difficulty):
    assert search_difficulty >= 0 and search_difficulty < 256
    assert check_job(job)
    # Fix: Note that we do not know exactly how to feed the fields from real blocks
    #      into this function.  It works with random bytes because we don't care
    #      about their order.
    midstate = sha256.cgminer_calc_midstate(int_to_lebytes(job['version'], 4) + job['previous block hash'] + job['merkle tree root'][0:28])
    return hf_hash_serial(midstate,
                          job['merkle tree root'][28:32],
                          job['timestamp'],
                          job['bits'],
                          job['starting nonce'],
                          job['nonce loops'],
                          job['ntime loops'],
                          search_difficulty, 0, 0, [0, 0, 0])

def nominal_hash_rate(clockrate):
    return 0.768 * clockrate - 0.03 * 0.768 * clockrate
