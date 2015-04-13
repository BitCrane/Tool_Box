import random
import sys
import time

from ..hf import HF_Error, HF_Thermal
from ..hf import Send, Receive
from ..hf import HF_Parse
from ..hf import HF_Frame, opcodes, opnames
from ..hf import HF_OP_USB_INIT, HF_OP_HASH, HF_OP_NONCE, HF_OP_STATUS, HF_OP_USB_NOTICE
from ..hf import SHUTDOWN, PROTOCOL_USB_MAPPED_SERIAL
from ..hf import decode_op_status_job_map, list_available_cores, rand_job
from ..hf import prepare_hf_hash_serial, check_nonce_work, sequence_a_leq_b

def noprint(x):
    pass

class HashTempTest():
    def __init__(self, talkusb, clockrate, printer=noprint):
        self.talkusb = talkusb
        self.clockrate = clockrate
        self.printer = printer

        self.transmitter = Send(talkusb)
        self.receiver = Receive(talkusb)

        self.restart()

    def restart(self):

        self.cores_per_die = None
        self.number_of_dies = None
        self.dies = None

        self.hash_rate_start = None
        self.hash_rate = 0
        self.total_hashes = 0
        self.total_errors = 0
        self.time_of_last_hash_report = None
        self.hash_report_interval = 2 # secs.

        self.op_usb_init_delay = 5.0
        self.last_op_usb_init_sent = None
        self.last_op_usb_init_count = 0

        self.test_search_difficulty = 34
        self.global_state = 'starting'
        self.random_source = "/dev/urandom"
        self.rndsrc = open(self.random_source, 'rb')
        self.parser = HF_Parse()

        random.seed(self.rndsrc.read(256))

    def one_cycle(self, throttle):
        try:
            # Fix: Every time we send, we want also to receive (to make sure nothing
            #      deadlocks), so the send and receive objects should be combined.
            # Fix: Do we want to have a delay in here or some sort of select() like thing?
            self.receiver.receive()
            self.transmitter.send([])

            traffic = self.receiver.read()
            if traffic:
                self.parser.input(traffic)
            
            if self.global_state == 'starting':
                if self.last_op_usb_init_sent is None or \
                        time.time() - self.last_op_usb_init_sent > self.op_usb_init_delay:
                    # Fix: Move this documentation elsewhere.
                    # core_address fields
                    # bits 2:0: Protocol to use
                    # bit  3:   Override configuration data
                    # bit  4:   PLL bypass
                    # bit  5:   Disable automatic ASIC initialization sequence
                    # bit  6:   At speed core test, return bitmap separately.
                    # bit  7:   Host supports gwq status shed_count
                    # If the uc thinks shed_supported is off, then it automatically disables
                    # core 95.  This only affects GWQ mode.  We turn it on so that core 95
                    # shows up on the working core map.
                    shed_supported = 0x80
                    core_address_field = PROTOCOL_USB_MAPPED_SERIAL | shed_supported
                    # Fix: Do this the correct way, probably by creating OP_USB_INIT class.
                    op_usb_init = HF_Frame({'operation_code': opcodes['OP_USB_INIT'], \
                                                   'core_address': core_address_field, \
                                                   'hdata': self.clockrate})
                    self.last_op_usb_init_count = self.last_op_usb_init_count + 1
                    self.printer("Sent OP_USB_INIT #%d." % (self.last_op_usb_init_count))
                    self.transmitter.send(op_usb_init.framebytes)
                    self.last_op_usb_init_sent = time.time()

                token = self.parser.next_token()
                if token:
                    if isinstance(token, HF_OP_USB_INIT):
                        # Fix: Do this the correct way, probably by creating OP_FAN class.
                        op_fan = HF_Frame({'operation_code': opcodes['OP_FAN'], \
                                            'core_address': 0x01, \
                                            'chip_address': 0xFF, \
                                            'hdata': 252})
                        self.printer("Sent OP_FAN.")
                        self.transmitter.send(op_fan.framebytes)

                        # parse OP_USB_INIT
                        self.number_of_dies = token.chip_address
                        self.cores_per_die = token.core_address
                        self.global_state = 'running'

                        self.printer("Got back initializing OP_USB_INIT packet.")
                        self.printer("Framebytes: %s" % (token.framebytes))
                        self.printer("Dies: %d" % (token.chip_address))
                        self.printer("Cores on each die: %d" % (token.core_address))
                        if token.hdata & 0xff == 1:
                            self.printer("Device ID: HashFast GN ASIC")
                        else:
                            self.printer("Strange Device ID: %d" % (token.hdata % 256))
                        self.printer("Reference clock rate: %d MHz" % (token.hdata >> 8))

                        # 16 bytes of struct hf_usb_init_base
                        # 16 bytes of struct hf_config_data
                        # Variable length core map
                        hf_usb_init_base_bytes = token.data[0:16]
                        hf_config_data_bytes = token.data[16:32]
                        coremap_bytes = token.data[32:]

                        # Fix: Don't exit, return failed value.
                        if hf_usb_init_base_bytes[8] != 0:
                            self.printer("operation_status not successful: %d" % (hf_usb_init_base_bytes[8]))
                            sys.exit(1)
                            
                        self.dies = [{'sequence': 0, 'work': {}, 'free pending slots': {},
                                      'free active slots': {}, 'core sequence': {}, 'last sequence': None,
                                      'monitor_data': None, 'thermal_cutoff': 0, 'active': 0, 'pending': 0}
                                     for i in range(token.chip_address)]

            elif self.global_state == 'running':
                # Fix: Perhaps average should be updated on every good nonce and then
                #      be available for reading.
                # Fix: Probably want to leave this to the calling process.
                if self.time_of_last_hash_report is not None:
                    report_elapsed = time.time() - self.time_of_last_hash_report
                    if report_elapsed > self.hash_report_interval:
#                        elapsed = time.time() - self.hash_rate_start
#                        hash_rate = self.total_hashes / elapsed
                        hash_rate_Ghs = self.hash_rate / 10**9
                        self.printer("Average hash rate: %f Gh/s" % (hash_rate_Ghs))
                        self.time_of_last_hash_report = time.time()

                while(self.parser.has_token()):
                    token = self.parser.next_token()
                    if token:
                        if isinstance(token, HF_OP_NONCE):
                            for nonce in token.nonces:
                                die = token.chip_address
                                if nonce.sequence in self.dies[die]['work']:
                                    work = self.dies[die]['work'][nonce.sequence]
                                    zerobits, regen_hash_expanded = check_nonce_work(work['job'], nonce.nonce)
                                    if zerobits >= self.test_search_difficulty:
                                        if self.hash_rate_start is None:
                                            self.hash_rate_start = time.time()
                                            self.time_of_last_hash_report = time.time()
                                        self.total_hashes += 2**self.test_search_difficulty
                                        elapsed = time.time() - self.hash_rate_start
                                        self.hash_rate = self.total_hashes / elapsed
                                        self.printer("Good nonce! (0x%08x) (zerobits %d) die: %d core: %d sequence: %d"
                                              % (nonce.nonce, zerobits, die, work['core'], nonce.sequence))
                                        
                                    else:
                                        self.total_errors += 1
                                        self.printer("Bad nonce. (%d) die: %d core: %d sequence: %d"
                                              % (nonce.nonce, die, work['core'], nonce.sequence))
                                else:
                                    self.total_errors += 1
                                    self.printer("Received unknown sequence number: %d" % (nonce.sequence))
                        elif isinstance(token, HF_OP_STATUS):
                            die = token.chip_address
                            last_sequence_seen = token.hdata
                            self.printer("Received OP_STATUS, die %d, last_sequence %d" % (die, last_sequence_seen))
                            assert die < len(self.dies)
                            #self.printer(str(token.thermal_cutoff))
                            if token.thermal_cutoff:
                                self.dies[die]['thermal_cutoff'] = token.thermal_cutoff
                                raise HF_Thermal("THERMAL CUTOFF, die %d" % (die))
                            active, pending = decode_op_status_job_map(token.coremap, self.cores_per_die)
                            pending_slots = len([x for x in pending if x == 1])
                            self.printer("die: %d pending slots filled: %s" % (die, pending_slots))
                            active_slots = len([x for x in active if x == 1])
                            self.printer("die: %d active slots filled: %d" % (die, active_slots))
                            raw_pending_core_list = list_available_cores(pending)
                            raw_active_core_list = list_available_cores(active)
                            pending_core_list = \
                                [x for x in raw_pending_core_list \
                                     if x not in self.dies[die]['core sequence'] \
                                     or sequence_a_leq_b(self.dies[die]['core sequence'][x], last_sequence_seen)]
                            active_core_list = \
                                [x for x in raw_active_core_list \
                                     if x not in self.dies[die]['core sequence'] \
                                     or sequence_a_leq_b(self.dies[die]['core sequence'][x], last_sequence_seen)]
                            random.shuffle(pending_core_list)
                            random.shuffle(active_core_list)
                            self.dies[die]['active'] = active_slots
                            self.dies[die]['pending'] = pending_slots
                            self.dies[die]['last sequence'] = last_sequence_seen
                            self.dies[die]['free pending slots'] = pending_core_list
                            self.dies[die]['free active slots'] = active_core_list
                            self.dies[die]['monitor_data'] = token.monitor_data
                            self.printer("T%d %d thr %d" % (die, token.monitor_data.die_temperature, throttle))
                        elif isinstance(token, HF_OP_USB_INIT):
                            self.printer("Received OP_USB_INIT packet, which was not expected.")
                        elif isinstance(token, HF_OP_USB_NOTICE):
                            self.printer("OP_USB_NOTICE notification code: %d extra data: %d message: %s"
                                  % (token.notification_code, token.extra_data, token.message))
                        elif isinstance(token, HF_Frame):
                            self.printer("Received HF_Frame() with %s operation." % (opnames[token.operation_code]))
                        elif isinstance(token, Garbage):
                            self.printer("Garbage: %d bytes" % (len(token.garbage)))
                        else:
                            raise HF_Error("Unexpected token type: %s" % (token))

                # Fix: Instead of having two separate full loops, we should set an active/pending
                #      flag and use the same infrastructure.
                # First stock the active slots.
                for die in range(self.number_of_dies):
                    this_die = self.dies[die]
                    receiver_throttle_counter = 0
                    for i in range(throttle):
                        if len(this_die['free active slots']) > 0:
                            this_die['free active slots'].pop()
                    for core in this_die['free active slots']:
                        job = rand_job(self.rndsrc)
                        sequence = this_die['sequence']
                        op_hash = HF_OP_HASH(die, core, sequence,
                                                prepare_hf_hash_serial(job, self.test_search_difficulty))
                        work = {'time': time.time(), 'job': job,
                                'search difficulty': self.test_search_difficulty,
                                'die': this_die, 'core': core}
                        # Fix: Note, overwrites previous sequence.
                        #      Maybe squirrel away in some archive of
                        #      past state?
                        this_die['core sequence'][core] = sequence
                        this_die['work'][sequence] = work
                        this_die['sequence'] = (this_die['sequence'] + 1) % 2**16
                        self.transmitter.send(op_hash.framebytes)
                        # Fix: See if there's a way to decrease time it takes to check
                        #      for incoming traffic so this hack can be avoided.
                        if receiver_throttle_counter % 10 == 0:
                            self.receiver.receive()
                        receiver_throttle_counter = receiver_throttle_counter + 1
                    this_die['free active slots'] = []
                # Next stock the pending slots.
                if throttle < 1:
                    for die in range(self.number_of_dies):
                        this_die = self.dies[die]
                        receiver_throttle_counter = 0
                        for core in this_die['free pending slots']:
                            sequence = this_die['sequence']
                            job = rand_job(self.rndsrc)
                            op_hash = HF_OP_HASH(die, core, sequence,
                                                    prepare_hf_hash_serial(job, self.test_search_difficulty))
                            work = {'time': time.time(), 'job': job,
                                    'search difficulty': self.test_search_difficulty,
                                    'die': this_die, 'core': core}
                            # Fix: Note, overwrites previous sequence.
                            #      Maybe squirrel away in some archive of
                            #      past state?
                            this_die['core sequence'][core] = sequence
                            this_die['work'][sequence] = work
                            this_die['sequence'] = (this_die['sequence'] + 1) % 2**16
                            self.transmitter.send(op_hash.framebytes)
                            # Fix: See if there's a way to decrease time it takes to check
                            #      for incoming traffic so this hack can be avoided.
                            if receiver_throttle_counter % 10 == 0:
                                self.receiver.receive()
                            receiver_throttle_counter = receiver_throttle_counter + 1
                        this_die['free pending slots'] = []
            # Fix: Put in assertion up front.
            else:
                raise HF_Error("Unknown global_state: %d" % (global_state))
            return True

        except KeyboardInterrupt:
            self.end()
            return False

        except HF_Thermal:
            self.printer("Generic exception handler: (%s, %s, %s)" % (sys.exc_info()[0], sys.exc_info()[1], sys.exc_info()[2]))
            self.end()
            return False

        except:
            self.printer("Generic exception handler: (%s, %s, %s)" % (sys.exc_info()[0], sys.exc_info()[1], sys.exc_info()[2]))
            self.end()
            return False

    def n_cycles(self, n):
        for i in range(n):
            rslt = self.one_cycle()
            if rslt != True:
                return rslt

    def end(self):
        op_usb_shutdown = HF_Frame({'operation_code': opcodes['OP_USB_SHUTDOWN'], 'hdata': 2})
        self.printer("Sent OP_USB_SHUTDOWN.")
        self.talkusb(SHUTDOWN, None, 0);
        return False

    def __del__(self):
        self.rndsrc.close()